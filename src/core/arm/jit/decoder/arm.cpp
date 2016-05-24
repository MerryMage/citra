// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

#include <boost/optional.hpp>

#include "common/assert.h"
#include "common/common_types.h"

#include "core/arm/jit/decoder/decoder.h"

namespace ArmDecoder {

namespace Impl {
    // Internal implementation for call
    template<size_t ...seq, typename Container, typename ...Args>
    void call_impl(std::integer_sequence<size_t, seq...>, Visitor* v, void (Visitor::*fn)(Args...), const Container& list) {
        using FunctionArgTypes = typename std::tuple<Args...>;
        // Here we static_cast each element in list to the corresponding argument type for fn.
        (v->*fn)(static_cast<typename std::tuple_element<seq, FunctionArgTypes>::type>(std::get<seq>(list))...);
    }

    /**
     * This function takes a member function of Visitor and calls it with the parameters specified in list.
     * @tparam NumArgs Number of arguments that the member function fn has.
     * @param v The Visitor.
     * @param fn Member function to call on v.
     * @param list List of arguments that will be splatted.
     */
    template<size_t NumArgs, typename Container, typename ...Args>
    void call(Visitor* v, void (Visitor::*fn)(Args...), const Container& list) {
        call_impl(typename std::index_sequence_for<Args...>{}, v, fn, list);
    }

    /// Function has NumArgs arguments
    template<size_t NumArgs, typename Function>
    struct MatcherImpl : ArmMatcher {
        std::array<u32, NumArgs> masks = {};
        std::array<size_t, NumArgs> shifts = {};
        Function fn = nullptr;
        void visit(Visitor *v, u32 inst) override {
            std::array<u32, NumArgs> values;
            std::transform(masks.begin(), masks.begin() + NumArgs, shifts.begin(), values.begin(),
                [inst](u32 mask, size_t shift) { return (inst & mask) >> shift; });
            call<NumArgs>(v, fn, values);
        }
    };
}

template<size_t NumArgs, typename Function>
static std::unique_ptr<ArmMatcher> MakeMatcher(const char* const format, Function fn) {
    ASSERT(strlen(format) == 32);

    auto ret = std::make_unique<Impl::MatcherImpl<NumArgs, Function>>();
    ret->fn = fn;
    ret->masks.fill(0);
    ret->shifts.fill(0);

    char ch = 0;
    int arg = -1;

    for (size_t i = 0; i < 32; i++) {
        const size_t bit_position = 31 - i;
        const u32 bit = 1 << bit_position;

        if (format[i] == '0') {
            // 0: A zero must be found here
            ret->bit_mask |= bit;
            ch = 0;
            continue;
        } else if (format[i] == '1') {
            // 1: A one must be found here
            ret->bit_mask |= bit;
            ret->expected |= bit;
            ch = 0;
            continue;
        } else if (format[i] == '-') {
            // -: Ignore this bit
            ch = 0;
            continue;
        }

        // Ban some characters
        ASSERT(format[i] != 'I');
        ASSERT(format[i] != 'l');
        ASSERT(format[i] != 'O');

        // otherwise: This bit is part of a field to extract

        // strings of the same character make up a field
        if (format[i] != ch) {
            arg++;
            ASSERT(arg < NumArgs);
            ch = format[i];
        }

        ret->masks[arg] |= bit;
        ret->shifts[arg] = bit_position;
    }

    ASSERT(arg == NumArgs - 1);

    return std::unique_ptr<ArmMatcher>(std::move(ret));
}

static const std::array<ArmInstruction, 221> arm_instruction_table = {{
    // Branch instructions
    { "BLX (immediate)",     MakeMatcher<2>("1111101hvvvvvvvvvvvvvvvvvvvvvvvv", &Visitor::BLX_imm)   }, // ARMv5
    { "BLX (register)",      MakeMatcher<2>("cccc000100101111111111110011mmmm", &Visitor::BLX_reg)   }, // ARMv5
    { "B",                   MakeMatcher<2>("cccc1010vvvvvvvvvvvvvvvvvvvvvvvv", &Visitor::B)         }, // all
    { "BL",                  MakeMatcher<2>("cccc1011vvvvvvvvvvvvvvvvvvvvvvvv", &Visitor::BL)        }, // all
    { "BX",                  MakeMatcher<2>("cccc000100101111111111110001mmmm", &Visitor::BX)        }, // ARMv4T
    { "BXJ",                 MakeMatcher<2>("cccc000100101111111111110010mmmm", &Visitor::BXJ)       }, // ARMv5J

    // Coprocessor instructions
    { "CDP2",                MakeMatcher<0>("11111110-------------------1----", &Visitor::CDP)       }, // ARMv5 (Generic Coprocessor)
    { "CDP",                 MakeMatcher<0>("----1110-------------------0----", &Visitor::CDP)       }, // ARMv2 (Generic Coprocessor)
    { "LDC2",                MakeMatcher<0>("1111110----1--------------------", &Visitor::LDC)       }, // ARMv5 (Generic Coprocessor)
    { "LDC",                 MakeMatcher<0>("----110----1--------------------", &Visitor::LDC)       }, // ARMv2 (Generic Coprocessor)
    { "MCR2",                MakeMatcher<0>("----1110---0---------------1----", &Visitor::MCR)       }, // ARMv5 (Generic Coprocessor)
    { "MCR",                 MakeMatcher<0>("----1110---0---------------1----", &Visitor::MCR)       }, // ARMv2 (Generic Coprocessor)
    { "MCRR2",               MakeMatcher<0>("111111000100--------------------", &Visitor::MCRR)      }, // ARMv6 (Generic Coprocessor)
    { "MCRR",                MakeMatcher<0>("----11000100--------------------", &Visitor::MCRR)      }, // ARMv5E (Generic Coprocessor)
    { "MRC2",                MakeMatcher<0>("11111110---1---------------1----", &Visitor::MRC)       }, // ARMv5 (Generic Coprocessor)
    { "MRC",                 MakeMatcher<0>("----1110---1---------------1----", &Visitor::MRC)       }, // ARMv2 (Generic Coprocessor)
    { "MRRC2",               MakeMatcher<0>("111111000101--------------------", &Visitor::MRRC)      }, // ARMv6 (Generic Coprocessor)
    { "MRRC",                MakeMatcher<0>("----11000101--------------------", &Visitor::MRRC)      }, // ARMv5E (Generic Coprocessor)
    { "STC2",                MakeMatcher<0>("1111110----0--------------------", &Visitor::STC)       }, // ARMv5 (Generic Coprocessor)
    { "STC",                 MakeMatcher<0>("----110----0--------------------", &Visitor::STC)       }, // ARMv2 (Generic Coprocessor)

    // Data Processing instructions
    { "ADC (imm)",           MakeMatcher<6>("cccc0010101Snnnnddddrrrrvvvvvvvv", &Visitor::ADC_imm)   }, // all
    { "ADC (reg)",           MakeMatcher<7>("cccc0000101Snnnnddddvvvvvrr0mmmm", &Visitor::ADC_reg)   }, // all
    { "ADC (rsr)",           MakeMatcher<7>("cccc0000101Snnnnddddssss0rr1mmmm", &Visitor::ADC_rsr)   }, // all
    { "ADD (imm)",           MakeMatcher<6>("cccc0010100Snnnnddddrrrrvvvvvvvv", &Visitor::ADD_imm)   }, // all
    { "ADD (reg)",           MakeMatcher<7>("cccc0000100Snnnnddddvvvvvrr0mmmm", &Visitor::ADD_reg)   }, // all
    { "ADD (rsr)",           MakeMatcher<7>("cccc0000100Snnnnddddssss0rr1mmmm", &Visitor::ADD_rsr)   }, // all
    { "AND (imm)",           MakeMatcher<6>("cccc0010000Snnnnddddrrrrvvvvvvvv", &Visitor::AND_imm)   }, // all
    { "AND (reg)",           MakeMatcher<7>("cccc0000000Snnnnddddvvvvvrr0mmmm", &Visitor::AND_reg)   }, // all
    { "AND (rsr)",           MakeMatcher<7>("cccc0000000Snnnnddddssss0rr1mmmm", &Visitor::AND_rsr)   }, // all
    { "BIC (imm)",           MakeMatcher<6>("cccc0011110Snnnnddddrrrrvvvvvvvv", &Visitor::BIC_imm)   }, // all
    { "BIC (reg)",           MakeMatcher<7>("cccc0001110Snnnnddddvvvvvrr0mmmm", &Visitor::BIC_reg)   }, // all
    { "BIC (rsr)",           MakeMatcher<7>("cccc0001110Snnnnddddssss0rr1mmmm", &Visitor::BIC_rsr)   }, // all
    { "CMN (imm)",           MakeMatcher<4>("cccc00110111nnnn0000rrrrvvvvvvvv", &Visitor::CMN_imm)   }, // all
    { "CMN (reg)",           MakeMatcher<5>("cccc00010111nnnn0000vvvvvrr0mmmm", &Visitor::CMN_reg)   }, // all
    { "CMN (rsr)",           MakeMatcher<5>("cccc00010111nnnn0000ssss0rr1mmmm", &Visitor::CMN_rsr)   }, // all
    { "CMP (imm)",           MakeMatcher<4>("cccc00110101nnnn0000rrrrvvvvvvvv", &Visitor::CMP_imm)   }, // all
    { "CMP (reg)",           MakeMatcher<5>("cccc00010101nnnn0000vvvvvrr0mmmm", &Visitor::CMP_reg)   }, // all
    { "CMP (rsr)",           MakeMatcher<5>("cccc00010101nnnn0000ssss0rr1mmmm", &Visitor::CMP_rsr)   }, // all
    { "EOR (imm)",           MakeMatcher<6>("cccc0010001Snnnnddddrrrrvvvvvvvv", &Visitor::EOR_imm)   }, // all
    { "EOR (reg)",           MakeMatcher<7>("cccc0000001Snnnnddddvvvvvrr0mmmm", &Visitor::EOR_reg)   }, // all
    { "EOR (rsr)",           MakeMatcher<7>("cccc0000001Snnnnddddssss0rr1mmmm", &Visitor::EOR_rsr)   }, // all
    { "MOV (imm)",           MakeMatcher<5>("cccc0011101S0000ddddrrrrvvvvvvvv", &Visitor::MOV_imm)   }, // all
    { "MOV (reg)",           MakeMatcher<6>("cccc0001101S0000ddddvvvvvrr0mmmm", &Visitor::MOV_reg)   }, // all
    { "MOV (rsr)",           MakeMatcher<6>("cccc0001101S0000ddddssss0rr1mmmm", &Visitor::MOV_rsr)   }, // all
    { "MVN (imm)",           MakeMatcher<5>("cccc0011111S0000ddddrrrrvvvvvvvv", &Visitor::MVN_imm)   }, // all
    { "MVN (reg)",           MakeMatcher<6>("cccc0001111S0000ddddvvvvvrr0mmmm", &Visitor::MVN_reg)   }, // all
    { "MVN (rsr)",           MakeMatcher<6>("cccc0001111S0000ddddssss0rr1mmmm", &Visitor::MVN_rsr)   }, // all
    { "ORR (imm)",           MakeMatcher<6>("cccc0011100Snnnnddddrrrrvvvvvvvv", &Visitor::ORR_imm)   }, // all
    { "ORR (reg)",           MakeMatcher<7>("cccc0001100Snnnnddddvvvvvrr0mmmm", &Visitor::ORR_reg)   }, // all
    { "ORR (rsr)",           MakeMatcher<7>("cccc0001100Snnnnddddssss0rr1mmmm", &Visitor::ORR_rsr)   }, // all
    { "RSB (imm)",           MakeMatcher<6>("cccc0010011Snnnnddddrrrrvvvvvvvv", &Visitor::RSB_imm)   }, // all
    { "RSB (reg)",           MakeMatcher<7>("cccc0000011Snnnnddddvvvvvrr0mmmm", &Visitor::RSB_reg)   }, // all
    { "RSB (rsr)",           MakeMatcher<7>("cccc0000011Snnnnddddssss0rr1mmmm", &Visitor::RSB_rsr)   }, // all
    { "RSC (imm)",           MakeMatcher<6>("cccc0010111Snnnnddddrrrrvvvvvvvv", &Visitor::RSC_imm)   }, // all
    { "RSC (reg)",           MakeMatcher<7>("cccc0000111Snnnnddddvvvvvrr0mmmm", &Visitor::RSC_reg)   }, // all
    { "RSC (rsr)",           MakeMatcher<7>("cccc0000111Snnnnddddssss0rr1mmmm", &Visitor::RSC_rsr)   }, // all
    { "SBC (imm)",           MakeMatcher<6>("cccc0010110Snnnnddddrrrrvvvvvvvv", &Visitor::SBC_imm)   }, // all
    { "SBC (reg)",           MakeMatcher<7>("cccc0000110Snnnnddddvvvvvrr0mmmm", &Visitor::SBC_reg)   }, // all
    { "SBC (rsr)",           MakeMatcher<7>("cccc0000110Snnnnddddssss0rr1mmmm", &Visitor::SBC_rsr)   }, // all
    { "SUB (imm)",           MakeMatcher<6>("cccc0010010Snnnnddddrrrrvvvvvvvv", &Visitor::SUB_imm)   }, // all
    { "SUB (reg)",           MakeMatcher<7>("cccc0000010Snnnnddddvvvvvrr0mmmm", &Visitor::SUB_reg)   }, // all
    { "SUB (rsr)",           MakeMatcher<7>("cccc0000010Snnnnddddssss0rr1mmmm", &Visitor::SUB_rsr)   }, // all
    { "TEQ (imm)",           MakeMatcher<4>("cccc00110011nnnn0000rrrrvvvvvvvv", &Visitor::TEQ_imm)   }, // all
    { "TEQ (reg)",           MakeMatcher<5>("cccc00010011nnnn0000vvvvvrr0mmmm", &Visitor::TEQ_reg)   }, // all
    { "TEQ (rsr)",           MakeMatcher<5>("cccc00010011nnnn0000ssss0rr1mmmm", &Visitor::TEQ_rsr)   }, // all
    { "TST (imm)",           MakeMatcher<4>("cccc00110001nnnn0000rrrrvvvvvvvv", &Visitor::TST_imm)   }, // all
    { "TST (reg)",           MakeMatcher<5>("cccc00010001nnnn0000vvvvvrr0mmmm", &Visitor::TST_reg)   }, // all
    { "TST (rsr)",           MakeMatcher<5>("cccc00010001nnnn0000ssss0rr1mmmm", &Visitor::TST_rsr)   }, // all

    // Exception Generating instructions
    { "BKPT",                MakeMatcher<3>("cccc00010010vvvvvvvvvvvv0111vvvv", &Visitor::BKPT)      }, // ARMv5
    { "SVC",                 MakeMatcher<2>("cccc1111vvvvvvvvvvvvvvvvvvvvvvvv", &Visitor::SVC)       }, // all
    { "UDF",                 MakeMatcher<0>("111001111111------------1111----", &Visitor::UDF)       }, // all

    // Extension instructions
    { "SXTB",                MakeMatcher<4>("cccc011010101111ddddrr000111mmmm", &Visitor::SXTB)      }, // ARMv6
    { "SXTB16",              MakeMatcher<4>("cccc011010001111ddddrr000111mmmm", &Visitor::SXTB16)    }, // ARMv6
    { "SXTH",                MakeMatcher<4>("cccc011010111111ddddrr000111mmmm", &Visitor::SXTH)      }, // ARMv6
    { "SXTAB",               MakeMatcher<5>("cccc01101010nnnnddddrr000111mmmm", &Visitor::SXTAB)     }, // ARMv6
    { "SXTAB16",             MakeMatcher<5>("cccc01101000nnnnddddrr000111mmmm", &Visitor::SXTAB16)   }, // ARMv6
    { "SXTAH",               MakeMatcher<5>("cccc01101011nnnnddddrr000111mmmm", &Visitor::SXTAH)     }, // ARMv6
    { "UXTB",                MakeMatcher<4>("cccc011011101111ddddrr000111mmmm", &Visitor::UXTB)      }, // ARMv6
    { "UXTB16",              MakeMatcher<4>("cccc011011001111ddddrr000111mmmm", &Visitor::UXTB16)    }, // ARMv6
    { "UXTH",                MakeMatcher<4>("cccc011011111111ddddrr000111mmmm", &Visitor::UXTH)      }, // ARMv6
    { "UXTAB",               MakeMatcher<5>("cccc01101110nnnnddddrr000111mmmm", &Visitor::UXTAB)     }, // ARMv6
    { "UXTAB16",             MakeMatcher<5>("cccc01101100nnnnddddrr000111mmmm", &Visitor::UXTAB16)   }, // ARMv6
    { "UXTAH",               MakeMatcher<5>("cccc01101111nnnnddddrr000111mmmm", &Visitor::UXTAH)     }, // ARMv6

    // Hint instructions
    { "PLD",                 MakeMatcher<0>("111101---101----1111------------", &Visitor::PLD)       }, // ARMv5E
    { "SEV",                 MakeMatcher<0>("----0011001000001111000000000100", &Visitor::SEV)       }, // ARMv6K
    { "WFE",                 MakeMatcher<0>("----0011001000001111000000000010", &Visitor::WFE)       }, // ARMv6K
    { "WFI",                 MakeMatcher<0>("----0011001000001111000000000011", &Visitor::WFI)       }, // ARMv6K
    { "YIELD",               MakeMatcher<0>("----0011001000001111000000000001", &Visitor::YIELD)     }, // ARMv6K

    // Synchronization Primitive instructions
    { "CLREX",               MakeMatcher<0>("11110101011111111111000000011111", &Visitor::CLREX)     }, // ARMv6K
    { "LDREX",               MakeMatcher<3>("cccc00011001nnnndddd111110011111", &Visitor::LDREX)     }, // ARMv6
    { "LDREXB",              MakeMatcher<3>("cccc00011101nnnndddd111110011111", &Visitor::LDREXB)    }, // ARMv6K
    { "LDREXD",              MakeMatcher<3>("cccc00011011nnnndddd111110011111", &Visitor::LDREXD)    }, // ARMv6K
    { "LDREXH",              MakeMatcher<3>("cccc00011111nnnndddd111110011111", &Visitor::LDREXH)    }, // ARMv6K
    { "STREX",               MakeMatcher<4>("cccc00011000nnnndddd11111001mmmm", &Visitor::STREX)     }, // ARMv6
    { "STREXB",              MakeMatcher<4>("cccc00011100nnnndddd11111001mmmm", &Visitor::STREXB)    }, // ARMv6K
    { "STREXD",              MakeMatcher<4>("cccc00011010nnnndddd11111001mmmm", &Visitor::STREXD)    }, // ARMv6K
    { "STREXH",              MakeMatcher<4>("cccc00011110nnnndddd11111001mmmm", &Visitor::STREXH)    }, // ARMv6K
    { "SWP",                 MakeMatcher<4>("cccc00010000nnnndddd00001001mmmm", &Visitor::SWP)       }, // ARMv2S (Deprecated in ARMv6)
    { "SWPB",                MakeMatcher<4>("cccc00010100nnnndddd00001001mmmm", &Visitor::SWPB)      }, // ARMv2S (Deprecated in ARMv6)

    // Load/Store instructions
    { "LDR (imm)",           MakeMatcher<7>("cccc010pu0w1nnnnddddvvvvvvvvvvvv", &Visitor::LDR_imm)   },
    { "LDR (reg)",           MakeMatcher<9>("cccc011pu0w1nnnnddddvvvvvrr0mmmm", &Visitor::LDR_reg)   },
    { "LDRB (imm)",          MakeMatcher<7>("cccc010pu1w1nnnnddddvvvvvvvvvvvv", &Visitor::LDRB_imm)  },
    { "LDRB (reg)",          MakeMatcher<9>("cccc011pu1w1nnnnddddvvvvvrr0mmmm", &Visitor::LDRB_reg)  },
    { "LDRBT (A1)",          MakeMatcher<0>("----0100-111--------------------", &Visitor::LDRBT)     },
    { "LDRBT (A2)",          MakeMatcher<0>("----0110-111---------------0----", &Visitor::LDRBT)     },
    { "LDRD (imm)",          MakeMatcher<8>("cccc000pu1w0nnnnddddvvvv1101vvvv", &Visitor::LDRD_imm)  }, // ARMv5E
    { "LDRD (reg)",          MakeMatcher<7>("cccc000pu0w0nnnndddd00001101mmmm", &Visitor::LDRD_reg)  }, // ARMv5E
    { "LDRH (imm)",          MakeMatcher<8>("cccc000pu1w1nnnnddddvvvv1011vvvv", &Visitor::LDRH_imm)  },
    { "LDRH (reg)",          MakeMatcher<7>("cccc000pu0w1nnnndddd00001011mmmm", &Visitor::LDRH_reg)  },
    { "LDRHT (A1)",          MakeMatcher<0>("----0000-111------------1011----", &Visitor::LDRHT)     },
    { "LDRHT (A2)",          MakeMatcher<0>("----0000-011--------00001011----", &Visitor::LDRHT)     },
    { "LDRSB (imm)",         MakeMatcher<8>("cccc000pu1w1nnnnddddvvvv1101vvvv", &Visitor::LDRSB_imm) },
    { "LDRSB (reg)",         MakeMatcher<7>("cccc000pu0w1nnnndddd00001101mmmm", &Visitor::LDRSB_reg) },
    { "LDRSBT (A1)",         MakeMatcher<0>("----0000-111------------1101----", &Visitor::LDRSBT)    },
    { "LDRSBT (A2)",         MakeMatcher<0>("----0000-011--------00001101----", &Visitor::LDRSBT)    },
    { "LDRSH (imm)",         MakeMatcher<8>("cccc000pu1w1nnnnddddvvvv1111vvvv", &Visitor::LDRSH_imm) },
    { "LDRSH (reg)",         MakeMatcher<7>("cccc000pu0w1nnnndddd00001111mmmm", &Visitor::LDRSH_reg) },
    { "LDRSHT (A1)",         MakeMatcher<0>("----0000-111------------1111----", &Visitor::LDRSHT)    },
    { "LDRSHT (A2)",         MakeMatcher<0>("----0000-011--------00001111----", &Visitor::LDRSHT)    },
    { "LDRT (A1)",           MakeMatcher<0>("----0100-011--------------------", &Visitor::LDRT)      },
    { "LDRT (A2)",           MakeMatcher<0>("----0110-011---------------0----", &Visitor::LDRT)      },
    { "STR (imm)",           MakeMatcher<7>("cccc010pu0w0nnnnddddvvvvvvvvvvvv", &Visitor::STR_imm)   },
    { "STR (reg)",           MakeMatcher<9>("cccc011pu0w0nnnnddddvvvvvrr0mmmm", &Visitor::STR_reg)   },
    { "STRB (imm)",          MakeMatcher<7>("cccc010pu1w0nnnnddddvvvvvvvvvvvv", &Visitor::STRB_imm)  },
    { "STRB (reg)",          MakeMatcher<9>("cccc011pu1w0nnnnddddvvvvvrr0mmmm", &Visitor::STRB_reg)  },
    { "STRBT (A1)",          MakeMatcher<0>("----0100-110--------------------", &Visitor::STRBT)     },
    { "STRBT (A2)",          MakeMatcher<0>("----0110-110---------------0----", &Visitor::STRBT)     },
    { "STRD (imm)",          MakeMatcher<8>("cccc000pu1w0nnnnddddvvvv1111vvvv", &Visitor::STRD_imm)  }, // ARMv5E
    { "STRD (reg)",          MakeMatcher<7>("cccc000pu0w0nnnndddd00001111mmmm", &Visitor::STRD_reg)  }, // ARMv5E
    { "STRH (imm)",          MakeMatcher<8>("cccc000pu1w0nnnnddddvvvv1011vvvv", &Visitor::STRH_imm)  },
    { "STRH (reg)",          MakeMatcher<7>("cccc000pu0w0nnnndddd00001011mmmm", &Visitor::STRH_reg)  },
    { "STRHT (A1)",          MakeMatcher<0>("----0000-110------------1011----", &Visitor::STRHT)     },
    { "STRHT (A2)",          MakeMatcher<0>("----0000-010--------00001011----", &Visitor::STRHT)     },
    { "STRT (A1)",           MakeMatcher<0>("----0100-010--------------------", &Visitor::STRT)      },
    { "STRT (A2)",           MakeMatcher<0>("----0110-010---------------0----", &Visitor::STRT)      },

    // Load/Store Multiple instructions
    { "LDM",                 MakeMatcher<6>("cccc100pu0w1nnnnxxxxxxxxxxxxxxxx", &Visitor::LDM)       }, // all
    { "LDM (usr reg)",       MakeMatcher<0>("----100--101--------------------", &Visitor::LDM_usr)   }, // all
    { "LDM (exce ret)",      MakeMatcher<0>("----100--1-1----1---------------", &Visitor::LDM_eret)  }, // all
    { "STM",                 MakeMatcher<6>("cccc100pu0w0nnnnxxxxxxxxxxxxxxxx", &Visitor::STM)       }, // all
    { "STM (usr reg)",       MakeMatcher<0>("----100--100--------------------", &Visitor::STM_usr)   }, // all

    // Miscellaneous instructions
    { "CLZ",                 MakeMatcher<3>("cccc000101101111dddd11110001mmmm", &Visitor::CLZ)       }, // ARMv5
    { "NOP",                 MakeMatcher<0>("----001100100000111100000000----", &Visitor::NOP)       }, // ARMv6K
    { "SEL",                 MakeMatcher<4>("cccc01101000nnnndddd11111011mmmm", &Visitor::SEL)       }, // ARMv6

    // Unsigned Sum of Absolute Differences instructions
    { "USAD8",               MakeMatcher<4>("cccc01111000dddd1111mmmm0001nnnn", &Visitor::USAD8)     }, // ARMv6
    { "USADA8",              MakeMatcher<5>("cccc01111000ddddaaaammmm0001nnnn", &Visitor::USADA8)    }, // ARMv6

    // Packing instructions
    { "PKHBT",               MakeMatcher<5>("cccc01101000nnnnddddvvvvv001mmmm", &Visitor::PKHBT)     }, // ARMv6K
    { "PKHTB",               MakeMatcher<5>("cccc01101000nnnnddddvvvvv101mmmm", &Visitor::PKHTB)     }, // ARMv6K

    // Reversal instructions
    { "REV",                 MakeMatcher<3>("cccc011010111111dddd11110011mmmm", &Visitor::REV)       }, // ARMv6
    { "REV16",               MakeMatcher<3>("cccc011010111111dddd11111011mmmm", &Visitor::REV16)     }, // ARMv6
    { "REVSH",               MakeMatcher<3>("cccc011011111111dddd11111011mmmm", &Visitor::REVSH)     }, // ARMv6

    // Saturation instructions
    { "SSAT",                MakeMatcher<6>("cccc0110101vvvvvddddvvvvvr01nnnn", &Visitor::SSAT)      }, // ARMv6
    { "SSAT16",              MakeMatcher<4>("cccc01101010vvvvdddd11110011nnnn", &Visitor::SSAT16)    }, // ARMv6
    { "USAT",                MakeMatcher<6>("cccc0110111vvvvvddddvvvvvr01nnnn", &Visitor::USAT)      }, // ARMv6
    { "USAT16",              MakeMatcher<4>("cccc01101110vvvvdddd11110011nnnn", &Visitor::USAT16)    }, // ARMv6

    // Multiply (Normal) instructions
    { "MLA",                 MakeMatcher<6>("cccc0000001Sddddaaaammmm1001nnnn", &Visitor::MLA)       }, // ARMv2
    { "MUL",                 MakeMatcher<5>("cccc0000000Sdddd0000mmmm1001nnnn", &Visitor::MUL)       }, // ARMv2

    // Multiply (Long) instructions
    { "SMLAL",               MakeMatcher<6>("cccc0000111Sddddaaaammmm1001nnnn", &Visitor::SMLAL)     }, // ARMv3M
    { "SMULL",               MakeMatcher<6>("cccc0000110Sddddaaaammmm1001nnnn", &Visitor::SMULL)     }, // ARMv3M
    { "UMAAL",               MakeMatcher<5>("cccc00000100ddddaaaammmm1001nnnn", &Visitor::UMAAL)     }, // ARMv6
    { "UMLAL",               MakeMatcher<6>("cccc0000101Sddddaaaammmm1001nnnn", &Visitor::UMLAL)     }, // ARMv3M
    { "UMULL",               MakeMatcher<6>("cccc0000100Sddddaaaammmm1001nnnn", &Visitor::UMULL)     }, // ARMv3M

    // Multiply (Halfword) instructions
    { "SMLALXY",             MakeMatcher<7>("cccc00010100ddddaaaammmm1xy0nnnn", &Visitor::SMLALxy)   }, // ARMv5xP
    { "SMLAXY",              MakeMatcher<7>("cccc00010000ddddaaaammmm1xy0nnnn", &Visitor::SMLAxy)    }, // ARMv5xP
    { "SMULXY",              MakeMatcher<6>("cccc00010110dddd0000mmmm1xy0nnnn", &Visitor::SMULxy)    }, // ARMv5xP

    // Multiply (Word by Halfword) instructions
    { "SMLAWY",              MakeMatcher<6>("cccc00010010ddddaaaammmm1y00nnnn", &Visitor::SMLAWy)    }, // ARMv5xP
    { "SMULWY",              MakeMatcher<5>("cccc00010010dddd0000mmmm1y10nnnn", &Visitor::SMULWy)    }, // ARMv5xP

    // Multiply (Most Significant Word) instructions
    { "SMMUL",               MakeMatcher<5>("cccc01110101dddd1111mmmm00R1nnnn", &Visitor::SMMUL)     }, // ARMv6
    { "SMMLA",               MakeMatcher<6>("cccc01110101ddddaaaammmm00R1nnnn", &Visitor::SMMLA)     }, // ARMv6
    { "SMMLS",               MakeMatcher<6>("cccc01110101ddddaaaammmm11R1nnnn", &Visitor::SMMLS)     }, // ARMv6

    // Multiply (Dual) instructions
    { "SMLAD",               MakeMatcher<6>("cccc01110000ddddaaaammmm00M1nnnn", &Visitor::SMLAD)     }, // ARMv6
    { "SMLALD",              MakeMatcher<6>("cccc01110100ddddaaaammmm00M1nnnn", &Visitor::SMLALD)    }, // ARMv6
    { "SMLSD",               MakeMatcher<6>("cccc01110000ddddaaaammmm01M1nnnn", &Visitor::SMLSD)     }, // ARMv6
    { "SMLSLD",              MakeMatcher<6>("cccc01110100ddddaaaammmm01M1nnnn", &Visitor::SMLSLD)    }, // ARMv6
    { "SMUAD",               MakeMatcher<5>("cccc01110000dddd1111mmmm00M1nnnn", &Visitor::SMUAD)     }, // ARMv6
    { "SMUSD",               MakeMatcher<5>("cccc01110000dddd1111mmmm01M1nnnn", &Visitor::SMUSD)     }, // ARMv6

    // Parallel Add/Subtract (Modulo) instructions
    { "SADD8",               MakeMatcher<4>("cccc01100001nnnndddd11111001mmmm", &Visitor::SADD8)     }, // ARMv6
    { "SADD16",              MakeMatcher<4>("cccc01100001nnnndddd11110001mmmm", &Visitor::SADD16)    }, // ARMv6
    { "SASX",                MakeMatcher<4>("cccc01100001nnnndddd11110011mmmm", &Visitor::SASX)      }, // ARMv6
    { "SSAX",                MakeMatcher<4>("cccc01100001nnnndddd11110101mmmm", &Visitor::SSAX)      }, // ARMv6
    { "SSUB8",               MakeMatcher<4>("cccc01100001nnnndddd11111111mmmm", &Visitor::SSUB8)     }, // ARMv6
    { "SSUB16",              MakeMatcher<4>("cccc01100001nnnndddd11110111mmmm", &Visitor::SSUB16)    }, // ARMv6
    { "UADD8",               MakeMatcher<4>("cccc01100101nnnndddd11111001mmmm", &Visitor::UADD8)     }, // ARMv6
    { "UADD16",              MakeMatcher<4>("cccc01100101nnnndddd11110001mmmm", &Visitor::UADD16)    }, // ARMv6
    { "UASX",                MakeMatcher<4>("cccc01100101nnnndddd11110011mmmm", &Visitor::UASX)      }, // ARMv6
    { "USAX",                MakeMatcher<4>("cccc01100101nnnndddd11110101mmmm", &Visitor::USAX)      }, // ARMv6
    { "USUB8",               MakeMatcher<4>("cccc01100101nnnndddd11111111mmmm", &Visitor::USUB8)     }, // ARMv6
    { "USUB16",              MakeMatcher<4>("cccc01100101nnnndddd11110111mmmm", &Visitor::USUB16)    }, // ARMv6

    // Parallel Add/Subtract (Saturating) instructions
    { "QADD8",               MakeMatcher<4>("cccc01100010nnnndddd11111001mmmm", &Visitor::QADD8)     }, // ARMv6
    { "QADD16",              MakeMatcher<4>("cccc01100010nnnndddd11110001mmmm", &Visitor::QADD16)    }, // ARMv6
    { "QASX",                MakeMatcher<4>("cccc01100010nnnndddd11110011mmmm", &Visitor::QASX)      }, // ARMv6
    { "QSAX",                MakeMatcher<4>("cccc01100010nnnndddd11110101mmmm", &Visitor::QSAX)      }, // ARMv6
    { "QSUB8",               MakeMatcher<4>("cccc01100010nnnndddd11111111mmmm", &Visitor::QSUB8)     }, // ARMv6
    { "QSUB16",              MakeMatcher<4>("cccc01100010nnnndddd11110111mmmm", &Visitor::QSUB16)    }, // ARMv6
    { "UQADD8",              MakeMatcher<4>("cccc01100110nnnndddd11111001mmmm", &Visitor::UQADD8)    }, // ARMv6
    { "UQADD16",             MakeMatcher<4>("cccc01100110nnnndddd11110001mmmm", &Visitor::UQADD16)   }, // ARMv6
    { "UQASX",               MakeMatcher<4>("cccc01100110nnnndddd11110011mmmm", &Visitor::UQASX)     }, // ARMv6
    { "UQSAX",               MakeMatcher<4>("cccc01100110nnnndddd11110101mmmm", &Visitor::UQSAX)     }, // ARMv6
    { "UQSUB8",              MakeMatcher<4>("cccc01100110nnnndddd11111111mmmm", &Visitor::UQSUB8)    }, // ARMv6
    { "UQSUB16",             MakeMatcher<4>("cccc01100110nnnndddd11110111mmmm", &Visitor::UQSUB16)   }, // ARMv6

    // Parallel Add/Subtract (Halving) instructions
    { "SHADD8",              MakeMatcher<4>("cccc01100011nnnndddd11111001mmmm", &Visitor::SHADD8)    }, // ARMv6
    { "SHADD16",             MakeMatcher<4>("cccc01100011nnnndddd11110001mmmm", &Visitor::SHADD16)   }, // ARMv6
    { "SHASX",               MakeMatcher<4>("cccc01100011nnnndddd11110011mmmm", &Visitor::SHASX)     }, // ARMv6
    { "SHSAX",               MakeMatcher<4>("cccc01100011nnnndddd11110101mmmm", &Visitor::SHSAX)     }, // ARMv6
    { "SHSUB8",              MakeMatcher<4>("cccc01100011nnnndddd11111111mmmm", &Visitor::SHSUB8)    }, // ARMv6
    { "SHSUB16",             MakeMatcher<4>("cccc01100011nnnndddd11110111mmmm", &Visitor::SHSUB16)   }, // ARMv6
    { "UHADD8",              MakeMatcher<4>("cccc01100111nnnndddd11111001mmmm", &Visitor::UHADD8)    }, // ARMv6
    { "UHADD16",             MakeMatcher<4>("cccc01100111nnnndddd11110001mmmm", &Visitor::UHADD16)   }, // ARMv6
    { "UHASX",               MakeMatcher<4>("cccc01100111nnnndddd11110011mmmm", &Visitor::UHASX)     }, // ARMv6
    { "UHSAX",               MakeMatcher<4>("cccc01100111nnnndddd11110101mmmm", &Visitor::UHSAX)     }, // ARMv6
    { "UHSUB8",              MakeMatcher<4>("cccc01100111nnnndddd11111111mmmm", &Visitor::UHSUB8)    }, // ARMv6
    { "UHSUB16",             MakeMatcher<4>("cccc01100111nnnndddd11110111mmmm", &Visitor::UHSUB16)   }, // ARMv6

    // Saturated Add/Subtract instructions
    { "QADD",                MakeMatcher<4>("cccc00010000nnnndddd00000101mmmm", &Visitor::QADD)      }, // ARMv5xP
    { "QSUB",                MakeMatcher<4>("cccc00010010nnnndddd00000101mmmm", &Visitor::QSUB)      }, // ARMv5xP
    { "QDADD",               MakeMatcher<4>("cccc00010100nnnndddd00000101mmmm", &Visitor::QDADD)     }, // ARMv5xP
    { "QDSUB",               MakeMatcher<4>("cccc00010110nnnndddd00000101mmmm", &Visitor::QDSUB)     }, // ARMv5xP

    // Status Register Access instructions
    { "CPS",                 MakeMatcher<0>("111100010000---00000000---0-----", &Visitor::CPS)       }, // ARMv6
    { "SETEND",              MakeMatcher<1>("1111000100000001000000e000000000", &Visitor::SETEND)    }, // ARMv6
    { "MRS",                 MakeMatcher<0>("----00010-00--------00--00000000", &Visitor::MRS)       }, // ARMv3
    { "MSR",                 MakeMatcher<0>("----00-10-10----1111------------", &Visitor::MSR)       }, // ARMv3
    { "RFE",                 MakeMatcher<0>("----0001101-0000---------110----", &Visitor::RFE)       }, // ARMv6
    { "SRS",                 MakeMatcher<0>("0000011--0-00000000000000001----", &Visitor::SRS)       }, // ARMv6
}};

boost::optional<const ArmInstruction&> DecodeArm(u32 i) {
    auto iterator = std::find_if(arm_instruction_table.cbegin(), arm_instruction_table.cend(),
        [i](const auto& instruction) { return instruction.Match(i); });

    return iterator != arm_instruction_table.cend() ? boost::make_optional<const ArmInstruction&>(*iterator) : boost::none;
}

} // namespace ArmDecoder