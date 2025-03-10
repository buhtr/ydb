# Generated by devtools/yamaker.

PROGRAM()

SUBSCRIBER(g:cpp-contrib)

VERSION(14.0.6)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/llvm14
    contrib/libs/llvm14/include
    contrib/libs/llvm14/lib/BinaryFormat
    contrib/libs/llvm14/lib/Demangle
    contrib/libs/llvm14/lib/MC
    contrib/libs/llvm14/lib/MC/MCDisassembler
    contrib/libs/llvm14/lib/MC/MCParser
    contrib/libs/llvm14/lib/MCA
    contrib/libs/llvm14/lib/Support
    contrib/libs/llvm14/lib/Target/AArch64/AsmParser
    contrib/libs/llvm14/lib/Target/AArch64/Disassembler
    contrib/libs/llvm14/lib/Target/AArch64/MCTargetDesc
    contrib/libs/llvm14/lib/Target/AArch64/TargetInfo
    contrib/libs/llvm14/lib/Target/AArch64/Utils
    contrib/libs/llvm14/lib/Target/ARM/AsmParser
    contrib/libs/llvm14/lib/Target/ARM/Disassembler
    contrib/libs/llvm14/lib/Target/ARM/MCTargetDesc
    contrib/libs/llvm14/lib/Target/ARM/TargetInfo
    contrib/libs/llvm14/lib/Target/ARM/Utils
    contrib/libs/llvm14/lib/Target/BPF/AsmParser
    contrib/libs/llvm14/lib/Target/BPF/Disassembler
    contrib/libs/llvm14/lib/Target/BPF/MCTargetDesc
    contrib/libs/llvm14/lib/Target/BPF/TargetInfo
    contrib/libs/llvm14/lib/Target/NVPTX/MCTargetDesc
    contrib/libs/llvm14/lib/Target/NVPTX/TargetInfo
    contrib/libs/llvm14/lib/Target/PowerPC/AsmParser
    contrib/libs/llvm14/lib/Target/PowerPC/Disassembler
    contrib/libs/llvm14/lib/Target/PowerPC/MCTargetDesc
    contrib/libs/llvm14/lib/Target/PowerPC/TargetInfo
    contrib/libs/llvm14/lib/Target/X86/AsmParser
    contrib/libs/llvm14/lib/Target/X86/Disassembler
    contrib/libs/llvm14/lib/Target/X86/MCTargetDesc
    contrib/libs/llvm14/lib/Target/X86/TargetInfo
)

ADDINCL(
    ${ARCADIA_BUILD_ROOT}/contrib/libs/llvm14/lib/Target/X86
    contrib/libs/llvm14/lib/Target/X86
    contrib/libs/llvm14/lib/Target/X86/MCA
    contrib/libs/llvm14/tools/llvm-mca
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCDIR(contrib/libs/llvm14)

SRCS(
    lib/Target/X86/MCA/X86CustomBehaviour.cpp
    tools/llvm-mca/CodeRegion.cpp
    tools/llvm-mca/CodeRegionGenerator.cpp
    tools/llvm-mca/PipelinePrinter.cpp
    tools/llvm-mca/Views/BottleneckAnalysis.cpp
    tools/llvm-mca/Views/DispatchStatistics.cpp
    tools/llvm-mca/Views/InstructionInfoView.cpp
    tools/llvm-mca/Views/InstructionView.cpp
    tools/llvm-mca/Views/RegisterFileStatistics.cpp
    tools/llvm-mca/Views/ResourcePressureView.cpp
    tools/llvm-mca/Views/RetireControlUnitStatistics.cpp
    tools/llvm-mca/Views/SchedulerStatistics.cpp
    tools/llvm-mca/Views/SummaryView.cpp
    tools/llvm-mca/Views/TimelineView.cpp
    tools/llvm-mca/llvm-mca.cpp
)

END()
