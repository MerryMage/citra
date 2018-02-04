// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <map>
#include <set>
#include <string>
#include <boost/icl/interval_set.hpp>
#include <boost/optional.hpp>
#include <nihstro/shader_bytecode.h>
#include <queue>
#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/renderer_opengl/gl_shader_decompiler.h"

namespace Pica {
namespace Shader {
namespace Decompiler {

using nihstro::Instruction;
using nihstro::OpCode;
using nihstro::RegisterType;
using nihstro::SourceRegister;
using nihstro::SwizzlePattern;

std::string GetCommonDeclarations() {
    return R"(
struct pica_uniforms {
    bvec4 b[4];
    uvec4 i[4];
    vec4 f[96];
};

struct {
    vec4 i[16];
    vec4 t[16];
    vec4 o[16];
} regs;

bool exec_shader();
)";
}

std::string DecompileProgram(const std::array<u32, MAX_PROGRAM_CODE_LENGTH>& program_code,
                             const std::array<u32, MAX_SWIZZLE_DATA_LENGTH>& swizzle_data,
                             u32 main_offset, const std::string& emit_cb,
                             const std::string& setemit_cb) {
    constexpr bool PRINT_DEBUG = true;
    constexpr u32 PROGRAM_END = MAX_PROGRAM_CODE_LENGTH;

    std::function<boost::optional<u32>(u32, u32)> find_end_instr =
        [&](u32 begin, u32 end) -> boost::optional<u32> {
        for (u32 offset = begin; offset < end; ++offset) {
            const Instruction instr = {program_code[offset]};
            switch (instr.opcode.Value()) {
            case OpCode::Id::END: {
                return offset;
            }
            case OpCode::Id::CALL: {
                const auto& opt = find_end_instr(instr.flow_control.dest_offset,
                                                 instr.flow_control.dest_offset +
                                                     instr.flow_control.num_instructions);
                if (opt) {
                    return *opt;
                }
                break;
            }
            case OpCode::Id::IFU:
            case OpCode::Id::IFC: {
                if (instr.flow_control.num_instructions != 0) {
                    const auto& opt_if = find_end_instr(offset + 1, instr.flow_control.dest_offset);
                    const auto& opt_else = find_end_instr(instr.flow_control.dest_offset,
                                                          instr.flow_control.dest_offset +
                                                              instr.flow_control.num_instructions);
                    if (opt_if && opt_else) {
                        return *opt_else;
                    }
                }
                offset = instr.flow_control.dest_offset + instr.flow_control.num_instructions - 1;
                break;
            }
            };
        }
        return boost::none;
    };

    auto main_end = find_end_instr(main_offset, PROGRAM_END);
    ASSERT(main_end);

    struct Subroutine {
        Subroutine(u32 begin_, u32 end_) : begin(begin_), end(end_) {}

        bool IsInScope(u32 offset) const {
            if (offset < begin || offset >= end) {
                return false;
            }
            for (auto& branch : branches) {
                if (offset >= branch.second->begin && offset < branch.second->end) {
                    return false;
                }
            }
            return true;
        }

        std::string GetName() const {
            return "sub_" + std::to_string(begin) + "_" + std::to_string(end);
        }

        u32 begin;
        u32 end;

        using SubroutineMap = std::map<std::pair<u32, u32>, const Subroutine*>;
        SubroutineMap calls;
        SubroutineMap branches;
        std::set<std::pair<Subroutine*, u32>> callers;

        bool return_to_dispatcher = false;
    };

    std::map<std::pair<u32, u32>, Subroutine> subroutines;
    auto get_routine = [&](u32 begin, u32 end) -> Subroutine& {
        return subroutines
            .emplace(std::make_pair(std::make_pair(begin, end), Subroutine{begin, end}))
            .first->second;
    };

    std::map<u32, u32> jump_to_map;
    std::map<u32, std::set<u32>> jump_from_map;

    std::queue<std::tuple<u32, u32, Subroutine*>> discover_queue;

    Subroutine& program_main = get_routine(main_offset, *main_end + 1);
    discover_queue.emplace(main_offset, *main_end + 1, &program_main);

    boost::icl::interval_set<u32> discovered_ranges;
    while (!discover_queue.empty()) {
        u32 begin;
        u32 end;
        Subroutine* routine;
        std::tie(begin, end, routine) = discover_queue.front();
        discover_queue.pop();

        if (end == PROGRAM_END) {
            boost::icl::discrete_interval<u32> find_interval{begin, PROGRAM_END};
            if (discovered_ranges.lower_bound(find_interval) != discovered_ranges.end()) {
                end = begin;
                auto it_pair = discovered_ranges.equal_range(find_interval);
                for (auto it = it_pair.first; it != it_pair.second; ++it) {
                    end = std::max(end, boost::icl::first(*it));
                }
            } else {
                end = find_end_instr(begin, PROGRAM_END).value_or(PROGRAM_END - 1) + 1;
            }
        }

        // delay jumps discovery
        std::set<u32> jumps;

        boost::icl::interval_set<u32> discover_ranges =
            boost::icl::interval_set<u32>({begin, end}) - discovered_ranges;

        for (auto& interval : discover_ranges) {
            for (u32 offset = boost::icl::first(interval); offset < boost::icl::last_next(interval);
                 ++offset) {
                discovered_ranges.insert({offset, offset + 1});

                const Instruction instr = {program_code[offset]};
                switch (instr.opcode.Value()) {
                case OpCode::Id::END: {
                    routine->return_to_dispatcher = true;
                    offset = boost::icl::last_next(interval);
                    break;
                }

                case OpCode::Id::JMPC:
                case OpCode::Id::JMPU: {
                    jump_to_map.emplace(offset, instr.flow_control.dest_offset);
                    jump_from_map[instr.flow_control.dest_offset].insert(offset);
                    jumps.insert(instr.flow_control.dest_offset);
                    break;
                }

                case OpCode::Id::CALL:
                case OpCode::Id::CALLU:
                case OpCode::Id::CALLC: {
                    std::pair<u32, u32> sub_range{instr.flow_control.dest_offset,
                                                  instr.flow_control.dest_offset +
                                                      instr.flow_control.num_instructions};
                    auto& sub = get_routine(sub_range.first, sub_range.second);
                    sub.callers.emplace(routine, offset + 1);
                    routine->calls[sub_range] = &sub;
                    discover_queue.emplace(sub_range.first, sub_range.second, &sub);
                    break;
                }

                case OpCode::Id::IFU:
                case OpCode::Id::IFC: {
                    const u32 if_offset = offset + 1;
                    const u32 else_offset = instr.flow_control.dest_offset;
                    const u32 endif_offset =
                        instr.flow_control.dest_offset + instr.flow_control.num_instructions;
                    ASSERT(else_offset > if_offset);

                    auto& sub_if = get_routine(if_offset, else_offset);
                    sub_if.callers.emplace(routine, endif_offset);
                    routine->branches[{if_offset, else_offset}] = &sub_if;
                    discover_queue.emplace(if_offset, else_offset, &sub_if);

                    if (instr.flow_control.num_instructions != 0) {
                        auto& sub_else = get_routine(else_offset, endif_offset);
                        sub_else.callers.emplace(routine, endif_offset);
                        routine->branches[{else_offset, endif_offset}] = &sub_else;
                        discover_queue.emplace(else_offset, endif_offset, &sub_else);
                    }

                    offset = endif_offset - 1;
                    break;
                }

                case OpCode::Id::LOOP: {
                    std::pair<u32, u32> sub_range{offset + 1, instr.flow_control.dest_offset + 1};
                    ASSERT(sub_range.second > sub_range.first);

                    auto& sub = get_routine(sub_range.first, sub_range.second);
                    sub.callers.emplace(routine, sub_range.second);
                    routine->branches[sub_range] = &sub;
                    discover_queue.emplace(sub_range.first, sub_range.second, &sub);

                    offset = instr.flow_control.dest_offset;
                    break;
                }
                }
            }
        }
        for (u32 jump_dest : jumps) {
            if (!routine->IsInScope(jump_dest)) {
                discover_queue.emplace(jump_dest, PROGRAM_END, &program_main);
            }
        }
    }

    std::function<bool(const Subroutine&)> is_callable = [&](const Subroutine& subroutine) {
        for (auto& jump : jump_to_map) {
            if (subroutine.IsInScope(jump.first) && !subroutine.IsInScope(jump.second)) {
                return false;
            }
        }
        for (auto& jump : jump_from_map) {
            if (subroutine.IsInScope(jump.first)) {
                for (auto jump_dest : jump.second) {
                    if (!subroutine.IsInScope(jump_dest)) {
                        return false;
                    }
                }
            }
        }
        for (auto& callee : subroutine.calls) {
            if (!is_callable(*callee.second)) {
                return false;
            }
        }
        for (auto& branch : subroutine.branches) {
            if (branch.second->begin < subroutine.begin || branch.second->end > subroutine.end ||
                !is_callable(*branch.second)) {
                return false;
            }
        }
        return true;
    };

    std::function<bool(const Subroutine&)> is_inline = [&](const Subroutine& subroutine) {
        if (subroutine.callers.size() > 1) {
            return false;
        }
        for (auto& jump : jump_from_map) {
            if (subroutine.IsInScope(jump.first)) {
                return false;
            }
        }
        return true;
    };

    std::function<void(Subroutine&)> propagate_return_to_dispatcher = [&](Subroutine& sub) {
        sub.return_to_dispatcher = true;
        for (auto& caller : sub.callers) {
            propagate_return_to_dispatcher(*caller.first);
        }
    };

    std::map<std::pair<u32, u32>, std::pair<const Subroutine*, bool>> callables;

    // fallbacks to dispatcher
    auto dispatcher_ranges = discovered_ranges;
    std::set<u32> entry_points;
    std::set<u32> end_offsets;

    for (auto& pair : subroutines) {
        auto& subroutine = pair.second;
        if (is_callable(subroutine)) {
            callables.emplace(std::make_pair(std::make_pair(subroutine.begin, subroutine.end),
                                             std::make_pair(&subroutine, is_inline(subroutine))));
            if (subroutine.return_to_dispatcher) {
                propagate_return_to_dispatcher(subroutine);
            }
        } else {
            entry_points.insert(subroutine.begin);
            end_offsets.insert(subroutine.end);
            entry_points.insert(subroutine.end);
            for (auto& caller : subroutine.callers) {
                entry_points.insert(caller.second);
            }
        }
    }

    bool main_callable = false;
    if (callables.find({program_main.begin, program_main.end}) != callables.end()) {
        main_callable = true;
    }

    if (!main_callable) {
        for (auto& jump : jump_from_map) {
            entry_points.insert(jump.first);
        }
    }

    std::string shader_source;
    int scope = 0;
    auto add_line = [&](const std::string& text) {
        ASSERT(scope >= 0);
        if (PRINT_DEBUG) {
            shader_source += std::string(static_cast<size_t>(scope) * 4, ' ');
        }
        shader_source += text + '\n';
    };

    add_line("vec4 dummy_vec4;");
    add_line("bvec2 conditional_code = bvec2(false);");
    add_line("ivec3 address_registers;\n");

    bool extra_lf = false;
    for (auto& pair : callables) {
        auto& subroutine = *pair.second.first;
        bool inlined = pair.second.second;
        if (!inlined || &subroutine == &program_main) {
            extra_lf = true;
            add_line("bool " + subroutine.GetName() + "();");
        }
    }
    if (extra_lf) {
        shader_source += '\n';
    }

    if (!main_callable) {
        add_line("struct {");
        ++scope;
        add_line("uint return_offset;");
        add_line("uint end_offset;");
        add_line("uint repeat_counter;");
        add_line("uint loop_increment;");
        add_line("uint loop_offset;");
        --scope;
        add_line("} call_stack[16];");
        add_line("uint stack_pos;");
        add_line("uint pc;");
        add_line("uint end_offset;\n");

        // do_call() start
        add_line("void do_call(uint dest_offset, "
                 "uint dest_end_offset, "
                 "uint return_offset, "
                 "uint repeat_counter, "
                 "uint loop_increment) {");
        ++scope;

        add_line("call_stack[stack_pos].return_offset = return_offset;");
        add_line("++stack_pos;");
        add_line("call_stack[stack_pos].loop_offset = dest_offset;");
        add_line("call_stack[stack_pos].end_offset = dest_end_offset;");
        add_line("call_stack[stack_pos].repeat_counter = repeat_counter;");
        add_line("call_stack[stack_pos].loop_increment = loop_increment;");
        add_line("pc = dest_offset;");
        add_line("end_offset = dest_end_offset;");

        // do_call() end
        --scope;
        add_line("}\n");

        // on_end_address() start
        add_line("void on_end_offset() {");
        ++scope;

        add_line("if (call_stack[stack_pos].repeat_counter != 0u) {");
        ++scope;
        add_line("--call_stack[stack_pos].repeat_counter;");
        add_line("pc = call_stack[stack_pos].loop_offset;");
        add_line("address_registers.z += int(call_stack[stack_pos].loop_increment);");
        --scope;
        add_line("} else {");
        ++scope;
        add_line("--stack_pos;");
        add_line("end_offset = call_stack[stack_pos].end_offset;");
        add_line("pc = call_stack[stack_pos].return_offset;");
        --scope;
        add_line("}");

        // on_end_address() end
        --scope;
        add_line("}\n");
    }

    // exec_shader() start
    add_line("bool exec_shader() {");
    ++scope;

    auto evaluate_condition = [](Instruction::FlowControlType flow_control) -> std::string {
        using Op = Instruction::FlowControlType::Op;

        std::string result_x =
            flow_control.refx.Value() ? "conditional_code.x" : "!conditional_code.x";
        std::string result_y =
            flow_control.refy.Value() ? "conditional_code.y" : "!conditional_code.y";

        switch (flow_control.op) {
        case Op::Or:
            return "(" + result_x + " || " + result_y + ")";
        case Op::And:
            return "(" + result_x + " && " + result_y + ")";
        case Op::JustX:
            return result_x;
        case Op::JustY:
            return result_y;
        default:
            UNREACHABLE();
            return "";
        }
    };

    auto get_source_register = [](const SourceRegister& source_reg,
                                  u32 address_register_index) -> std::string {
        std::string index = std::to_string(source_reg.GetIndex());
        if (address_register_index != 0) {
            index += std::string(" + address_registers.") + "xyz"[address_register_index - 1];
        }

        switch (source_reg.GetRegisterType()) {
        case RegisterType::Input:
            return "regs.i[" + index + "]";
        case RegisterType::Temporary:
            return "regs.t[" + index + "]";
        case RegisterType::FloatUniform:
            return "uniforms.f[" + index + "]";
        default:
            return "dummy_vec4";
        }
    };

    auto selector_to_string = [](const auto& selector_getter) -> std::string {
        std::string out;
        for (int i = 0; i < 4; ++i) {
            SwizzlePattern::Selector selector = selector_getter(i);
            switch (selector) {
            case SwizzlePattern::Selector::x:
                out += "x";
                break;
            case SwizzlePattern::Selector::y:
                out += "y";
                break;
            case SwizzlePattern::Selector::z:
                out += "z";
                break;
            case SwizzlePattern::Selector::w:
                out += "w";
                break;
            }
        }
        return out;
    };

    auto dest_components_total = [](const SwizzlePattern& swizzle, int components = 4) {
        int ret = 0;
        for (int i = 0; i < components; ++i) {
            if (swizzle.DestComponentEnabled(i)) {
                ++ret;
            }
        }
        return ret;
    };

    auto apply_dest_mask = [&dest_components_total](const std::string& vec,
                                                    const SwizzlePattern& swizzle,
                                                    int components = 4) -> std::string {
        if (!dest_components_total(swizzle, components)) {
            return "dummy_vec4";
        }
        std::string out = "(" + vec + ").";
        for (int i = 0; i < components; ++i) {
            if (swizzle.DestComponentEnabled(i))
                out += "xyzw"[i];
        }
        return out;
    };

    auto get_uniform_bool = [&](u32 index) -> std::string {
        if (!emit_cb.empty() && index == 15) {
            // The uniform b15 is set to true after every geometry shader invocation.
            return "(gl_PrimitiveIDIn == 0 ? uniforms.b[3].w : true)";
        }
        return "uniforms.b[" + std::to_string(index / 4) + "]." + "xyzw"[index % 4];
    };

    auto pica_mul = [](const std::string& lhs, const std::string& rhs) -> std::string {
        // TODO: inf * 0 == 0?
        return "(" + lhs + " * " + rhs + ")";
    };

    auto pica_min = [](const std::string& lhs, const std::string& rhs) -> std::string {
        // TODO: NaN == rhs?
        return "min(" + lhs + ", " + rhs + ")";
    };

    auto pica_max = [](const std::string& lhs, const std::string& rhs) -> std::string {
        // TODO: NaN == rhs?
        return "max(" + lhs + ", " + rhs + ")";
    };

    std::function<void(const std::pair<u32, u32>&)> call_subroutine;

    auto compile_instr = [&](u32 offset, auto jumper_fn) -> u32 {
        const Instruction instr = {program_code[offset]};

        size_t swizzle_offset = instr.opcode.Value().GetInfo().type == OpCode::Type::MultiplyAdd
                                    ? instr.mad.operand_desc_id
                                    : instr.common.operand_desc_id;
        const SwizzlePattern swizzle = {swizzle_data[swizzle_offset]};

        if (PRINT_DEBUG) {
            add_line("// " + std::to_string(offset) + ": " + instr.opcode.Value().GetInfo().name +
                     " instr: " + std::to_string(instr.hex) +
                     " swizzle: " + std::to_string(swizzle.hex));
        }

        switch (instr.opcode.Value().GetInfo().type) {
        case OpCode::Type::Arithmetic: {
            const bool is_inverted =
                (0 != (instr.opcode.Value().GetInfo().subtype & OpCode::Info::SrcInversed));

            std::string src1 = swizzle.negate_src1 ? "-" : "";
            src1 += get_source_register(instr.common.GetSrc1(is_inverted),
                                        !is_inverted * instr.common.address_register_index);
            src1 +=
                "." + selector_to_string([&](int comp) { return swizzle.GetSelectorSrc1(comp); });

            std::string src2 = swizzle.negate_src2 ? "-" : "";
            src2 += get_source_register(instr.common.GetSrc2(is_inverted),
                                        is_inverted * instr.common.address_register_index);
            src2 +=
                "." + selector_to_string([&](int comp) { return swizzle.GetSelectorSrc2(comp); });

            std::string dest =
                (instr.common.dest.Value() < 0x10)
                    ? "regs.o[" + std::to_string(instr.common.dest.Value().GetIndex()) + "]"
                    : (instr.common.dest.Value() < 0x20)
                          ? "regs.t[" + std::to_string(instr.common.dest.Value().GetIndex()) + "]"
                          : "dummy_vec4";

            switch (instr.opcode.Value().EffectiveOpCode()) {
            case OpCode::Id::ADD: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask(src1 + " + " + src2, swizzle) + ";");
                break;
            }

            case OpCode::Id::MUL: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask(pica_mul(src1, src2), swizzle) + ";");
                break;
            }

            case OpCode::Id::FLR: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("floor(" + src1 + ")", swizzle) + ";");
                break;
            }

            case OpCode::Id::MAX: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask(pica_max(src1, src2), swizzle) + ";");
                break;
            }

            case OpCode::Id::MIN: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask(pica_min(src1, src2), swizzle) + ";");
                break;
            }

            case OpCode::Id::DP3:
            case OpCode::Id::DP4:
            case OpCode::Id::DPH:
            case OpCode::Id::DPHI: {
                OpCode::Id opcode = instr.opcode.Value().EffectiveOpCode();
                std::string src1_ = src1;
                std::string src2_ = src2;
                if (opcode == OpCode::Id::DPH || opcode == OpCode::Id::DPHI)
                    src1_ = "vec4(" + src1 + ".xyz, 1.0)";

                std::string tmp = "vec4(1.0)";
                if (opcode == OpCode::Id::DP3) {
                    src1_ = "vec3(" + src1_ + ")";
                    src2_ = "vec3(" + src2_ + ")";
                    tmp = "vec3(1.0)";
                }

                std::string dot = "dot(" + pica_mul(src1_, src2_) + ", " + tmp + ")";
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("vec4(" + dot + ")", swizzle) + ";");
                break;
            }

            case OpCode::Id::RCP: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("vec4(1.0 / " + src1 + ".x)", swizzle) + ";");
                break;
            }

            case OpCode::Id::RSQ: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("vec4(inversesqrt(" + src1 + ".x))", swizzle) + ";");
                break;
            }

            case OpCode::Id::MOVA: {
                add_line(apply_dest_mask("address_registers", swizzle, 2) + " = " +
                         apply_dest_mask("ivec2(" + src1 + ")", swizzle, 2) + ";");
                break;
            }

            case OpCode::Id::MOV: {
                add_line(apply_dest_mask(dest, swizzle) + " = " + apply_dest_mask(src1, swizzle) +
                         ";");
                break;
            }

            case OpCode::Id::SGE:
            case OpCode::Id::SGEI: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("mix(vec4(0.0), vec4(1.0), greaterThanEqual(" + src1 +
                                             "," + src2 + "))",
                                         swizzle) +
                         ";");
            } break;

            case OpCode::Id::SLT:
            case OpCode::Id::SLTI: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("mix(vec4(0.0), vec4(1.0), lessThan(" + src1 + "," + src2 +
                                             "))",
                                         swizzle) +
                         ";");
            } break;

            case OpCode::Id::CMP:
                for (int i = 0; i < 2; ++i) {
                    std::string comp = i == 0 ? ".x" : ".y";
                    std::string op_str;
                    auto op = (i == 0) ? instr.common.compare_op.x.Value()
                                       : instr.common.compare_op.y.Value();
                    switch (op) {
                    case Instruction::Common::CompareOpType::Equal:
                        op_str = " == ";
                        break;
                    case Instruction::Common::CompareOpType::NotEqual:
                        op_str = " != ";
                        break;
                    case Instruction::Common::CompareOpType::LessThan:
                        op_str = " < ";
                        break;
                    case Instruction::Common::CompareOpType::LessEqual:
                        op_str = " <= ";
                        break;
                    case Instruction::Common::CompareOpType::GreaterThan:
                        op_str = " > ";
                        break;
                    case Instruction::Common::CompareOpType::GreaterEqual:
                        op_str = " >= ";
                        break;
                    default:
                        LOG_ERROR(HW_GPU, "Unknown compare mode %x", static_cast<int>(op));
                        break;
                    }
                    add_line("conditional_code" + comp + " = (" + src1 + comp + op_str + src2 +
                             comp + ");");
                }
                break;

            case OpCode::Id::EX2: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("vec4(exp2(" + src1 + ".x))", swizzle) + ";");
                break;
            }

            case OpCode::Id::LG2: {
                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask("vec4(log2(" + src1 + ".x))", swizzle) + ";");
                break;
            }

            default:
                LOG_ERROR(HW_GPU, "Unhandled arithmetic instruction: 0x%02x (%s): 0x%08x",
                          (int)instr.opcode.Value().EffectiveOpCode(),
                          instr.opcode.Value().GetInfo().name, instr.hex);
                DEBUG_ASSERT(false);
                break;
            }

            break;
        }

        case OpCode::Type::MultiplyAdd: {
            if ((instr.opcode.Value().EffectiveOpCode() == OpCode::Id::MAD) ||
                (instr.opcode.Value().EffectiveOpCode() == OpCode::Id::MADI)) {
                bool is_inverted = (instr.opcode.Value().EffectiveOpCode() == OpCode::Id::MADI);

                std::string src1 = swizzle.negate_src1 ? "-" : "";
                src1 += get_source_register(instr.mad.GetSrc1(is_inverted), 0);
                src1 += "." +
                        selector_to_string([&](int comp) { return swizzle.GetSelectorSrc1(comp); });

                std::string src2 = swizzle.negate_src2 ? "-" : "";
                src2 += get_source_register(instr.mad.GetSrc2(is_inverted),
                                            !is_inverted * instr.mad.address_register_index);
                src2 += "." +
                        selector_to_string([&](int comp) { return swizzle.GetSelectorSrc2(comp); });

                std::string src3 = swizzle.negate_src3 ? "-" : "";
                src3 += get_source_register(instr.mad.GetSrc3(is_inverted),
                                            is_inverted * instr.mad.address_register_index);
                src3 += "." +
                        selector_to_string([&](int comp) { return swizzle.GetSelectorSrc3(comp); });

                std::string dest =
                    (instr.mad.dest.Value() < 0x10)
                        ? "regs.o[" + std::to_string(instr.mad.dest.Value().GetIndex()) + "]"
                        : (instr.mad.dest.Value() < 0x20)
                              ? "regs.t[" + std::to_string(instr.mad.dest.Value().GetIndex()) + "]"
                              : "dummy_vec4";

                add_line(apply_dest_mask(dest, swizzle) + " = " +
                         apply_dest_mask(pica_mul(src1, src2) + " + " + src3, swizzle) + ";");
            } else {
                LOG_ERROR(HW_GPU, "Unhandled multiply-add instruction: 0x%02x (%s): 0x%08x",
                          (int)instr.opcode.Value().EffectiveOpCode(),
                          instr.opcode.Value().GetInfo().name, instr.hex);
            }
            break;
        }

        default: {
            switch (instr.opcode.Value()) {
            case OpCode::Id::END: {
                add_line("return true;");
                offset = PROGRAM_END;
                break;
            }

            case OpCode::Id::JMPC:
            case OpCode::Id::JMPU: {
                std::string condition;
                if (instr.opcode.Value() == OpCode::Id::JMPC) {
                    condition = evaluate_condition(instr.flow_control);
                } else {
                    bool invert_test = instr.flow_control.num_instructions & 1;
                    condition = (invert_test ? "!" : "") +
                                get_uniform_bool(instr.flow_control.bool_uniform_id);
                }

                add_line("if (" + condition + ") {");
                ++scope;
                jumper_fn(instr.flow_control.dest_offset);

                --scope;
                add_line("}");
                break;
            }

            case OpCode::Id::CALL:
            case OpCode::Id::CALLC:
            case OpCode::Id::CALLU: {
                std::string condition;
                if (instr.opcode.Value() == OpCode::Id::CALLC) {
                    condition = evaluate_condition(instr.flow_control);
                } else if (instr.opcode.Value() == OpCode::Id::CALLU) {
                    condition = get_uniform_bool(instr.flow_control.bool_uniform_id);
                }

                if (!condition.empty()) {
                    add_line("if (" + condition + ") {");
                    ++scope;
                }

                std::pair<u32, u32> sub_range{instr.flow_control.dest_offset,
                                              instr.flow_control.dest_offset +
                                                  instr.flow_control.num_instructions};

                if (callables.find(sub_range) == callables.end()) {
                    add_line("do_call(" + std::to_string(sub_range.first) + "u, " +
                             std::to_string(sub_range.second) + "u, " + std::to_string(offset + 1) +
                             "u, 0u, 0u);");
                    add_line("break;");
                } else {
                    call_subroutine(sub_range);
                }

                if (!condition.empty()) {
                    --scope;
                    add_line("}");
                }
                break;
            }

            case OpCode::Id::NOP: {
                break;
            }

            case OpCode::Id::IFC:
            case OpCode::Id::IFU: {
                std::string condition;
                if (instr.opcode.Value() == OpCode::Id::IFC) {
                    condition = evaluate_condition(instr.flow_control);
                } else {
                    condition = get_uniform_bool(instr.flow_control.bool_uniform_id);
                }

                const u32 if_offset = offset + 1;
                const u32 else_offset = instr.flow_control.dest_offset;
                const u32 endif_offset =
                    instr.flow_control.dest_offset + instr.flow_control.num_instructions;

                add_line("if (" + condition + ") {");
                ++scope;

                if (callables.find({if_offset, else_offset}) == callables.end()) {
                    add_line("do_call(" + std::to_string(if_offset) + "u, " +
                             std::to_string(else_offset) + "u, " + std::to_string(endif_offset) +
                             "u, 0u, 0u);");
                } else {
                    call_subroutine({if_offset, else_offset});
                    offset = else_offset - 1;
                }

                if (callables.find({if_offset, else_offset}) == callables.end() ||
                    instr.flow_control.num_instructions != 0) {
                    --scope;
                    add_line("} else {");
                    ++scope;
                }

                if (instr.flow_control.num_instructions != 0 &&
                    callables.find({else_offset, endif_offset}) == callables.end()) {
                    add_line("pc = " + std::to_string(else_offset) + "u;");
                    add_line("break;");
                } else {
                    if (instr.flow_control.num_instructions != 0) {
                        call_subroutine({else_offset, endif_offset});
                        if (callables.find({if_offset, else_offset}) != callables.end()) {
                            offset = endif_offset - 1;
                        }
                    }
                    if (callables.find({if_offset, else_offset}) == callables.end()) {
                        add_line("pc = " + std::to_string(endif_offset) + "u;");
                        add_line("break;");
                    }
                }

                --scope;
                add_line("}");
                break;
            }

            case OpCode::Id::LOOP: {
                std::string int_uniform =
                    "uniforms.i[" + std::to_string(instr.flow_control.int_uniform_id) + "]";

                add_line("address_registers.z = int(" + int_uniform + ".y);");

                std::pair<u32, u32> sub_range{offset + 1, instr.flow_control.dest_offset + 1};

                if (callables.find(sub_range) == callables.end()) {
                    add_line("do_call(" + std::to_string(sub_range.first) + "u, " +
                             std::to_string(sub_range.second) + "u, " +
                             std::to_string(sub_range.second) + "u, " + int_uniform + ".x, " +
                             int_uniform + ".z);");
                } else {
                    std::string loop_var = "loop" + std::to_string(offset);
                    add_line("for (uint " + loop_var + " = 0u; " + loop_var + " <= " + int_uniform +
                             ".x; address_registers.z += int(" + int_uniform + ".z), ++" +
                             loop_var + ") {");
                    ++scope;
                    call_subroutine(sub_range);
                    --scope;
                    add_line("}");

                    offset = sub_range.second - 1;
                }

                break;
            }

            case OpCode::Id::EMIT: {
                if (!emit_cb.empty()) {
                    add_line(emit_cb + "();");
                }
                break;
            }

            case OpCode::Id::SETEMIT: {
                if (!setemit_cb.empty()) {
                    ASSERT(instr.setemit.vertex_id < 3);
                    add_line(setemit_cb + "(" + std::to_string(instr.setemit.vertex_id) + "u, " +
                             ((instr.setemit.prim_emit != 0) ? "true" : "false") + ", " +
                             ((instr.setemit.winding != 0) ? "true" : "false") + ");");
                }
                break;
            }

            default: {
                LOG_ERROR(HW_GPU, "Unhandled instruction: 0x%02x (%s): 0x%08x",
                          (int)instr.opcode.Value().EffectiveOpCode(),
                          instr.opcode.Value().GetInfo().name, instr.hex);
                break;
            }
            }

            break;
        }
        }
        return offset + 1;
    };

    call_subroutine = [&](const std::pair<u32, u32>& range) {
        auto it = callables.find(range);
        ASSERT(it != callables.end());
        auto& subroutine = *it->second.first;
        const bool inlined = it->second.second;
        if (inlined) {
            for (u32 program_counter = subroutine.begin; program_counter < subroutine.end;) {
                program_counter =
                    compile_instr(program_counter, [&](u32 offset) { UNREACHABLE(); });
            }
        } else {
            if (subroutine.return_to_dispatcher) {
                add_line("if (" + subroutine.GetName() + "()) { return true; }");
            } else {
                add_line(subroutine.GetName() + "();");
            }
        }
    };

    if (!main_callable) {
        add_line("stack_pos = 0u;");
        add_line("pc = " + std::to_string(main_offset) + "u;");
        add_line("end_offset = 0xFFFFFFFFu;");
        add_line("call_stack[0].end_offset = 0xFFFFFFFFu;");
        add_line("call_stack[0].repeat_counter = 0u;\n");

        // main loop start
        add_line("while (true) {");
        ++scope;

        // jumptable start
        add_line("switch (pc) {");

        for (auto interval : discovered_ranges) {
            for (auto it = entry_points.lower_bound(boost::icl::first(interval));
                 it != entry_points.end() && *it < boost::icl::last_next(interval); ++it) {
                auto next_it = it;
                ++next_it;

                u32 end =
                    (next_it == entry_points.end() || *next_it > boost::icl::last_next(interval))
                        ? boost::icl::last_next(interval)
                        : *next_it;

                add_line("case " + std::to_string(*it) + "u: {");
                ++scope;

                for (u32 program_counter = *it; program_counter < end;) {
                    if (end_offsets.find(program_counter) != end_offsets.end()) {
                        add_line("if (end_offset == " + std::to_string(program_counter) +
                                 "u) { on_end_offset(); break; }");
                    }
                    program_counter = compile_instr(program_counter, [&](u32 offset) {
                        add_line("pc = " + std::to_string(offset) + "u;");
                        add_line("break;");
                    });
                }

                --scope;
                add_line("}");
            }

            add_line("case " + std::to_string(boost::icl::last_next(interval)) +
                     "u: { if (end_offset == " + std::to_string(boost::icl::last_next(interval)) +
                     "u) { on_end_offset(); break; } return true; }");
        }

        // jumptable end
        add_line("default: return true;");
        add_line("}");

        // main loop end
        --scope;
        add_line("}");
    } else {
        add_line(program_main.GetName() + "();");
    }

    // exec_shader() end
    add_line("return true;");
    --scope;
    add_line("}\n");
    ASSERT(!scope);

    for (auto& pair : callables) {
        auto& subroutine = *pair.second.first;
        const bool inlined = pair.second.second;
        if (inlined && &subroutine != &program_main) {
            continue;
        }

        std::set<u32> labels;
        for (auto& jump : jump_from_map) {
            if (subroutine.IsInScope(jump.first)) {
                for (u32 offset = jump.first; offset < subroutine.end; ++offset) {
                    const Instruction instr = {program_code[offset]};
                    if (instr.opcode.Value() != OpCode::Id::NOP) {
                        labels.insert(jump.first);
                        break;
                    }
                }
            }
        }

        add_line("bool " + subroutine.GetName() + "() {");
        ++scope;
        if (!labels.empty()) {
            labels.insert(subroutine.begin);
            add_line("uint jmp_to = " + std::to_string(subroutine.begin) + "u;");
            add_line("while (true) {");
            ++scope;
            add_line("switch (jmp_to) {");
        }

        u32 program_counter = subroutine.begin;
        while (program_counter < subroutine.end) {
            if (!labels.empty() && labels.find(program_counter) != labels.end()) {
                if (program_counter != subroutine.begin) {
                    --scope;
                    add_line("}");
                }
                add_line("case " + std::to_string(program_counter) + "u: {");
                ++scope;
            }
            program_counter = compile_instr(program_counter, [&](u32 offset) {
                if (labels.find(offset) == labels.end()) {
                    add_line("return false;");
                } else {
                    add_line("{ jmp_to = " + std::to_string(offset) + "u; break; }");
                }
            });
        }

        if (!labels.empty()) {
            --scope;
            add_line("}");
            add_line("default: return false;");
            add_line("}");
            --scope;
            add_line("}");
            add_line("return false;");
        } else if (program_counter <= PROGRAM_END) {
            add_line("return false;");
        }

        --scope;
        add_line("}\n");
    }
    ASSERT(!scope);

    return shader_source;
}

} // namespace Decompiler
} // namespace Shader
} // namespace Pica
