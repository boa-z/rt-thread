import os
import shutil
import subprocess

# Toolchain options
ARCH        = 'risc-v'
CPU         = 'e9xx'
VENDOR      = ''
CROSS_TOOL  = 'gcc'

RTT_ROOT = os.getenv('RTT_ROOT') or os.path.normpath(
    os.path.join(os.getcwd(), '..', '..', '..'))

if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')

if CROSS_TOOL == 'gcc':
    PLATFORM  = 'gcc'
    EXEC_PATH = os.getenv('RTT_EXEC_PATH') or ''
else:
    print('Please make sure your toolchain is GNU GCC!')
    exit(0)

BUILD = os.getenv('RTT_BUILD', 'release')

if PLATFORM == 'gcc':
    PREFIX  = os.getenv('RTT_CC_PREFIX') or 'riscv64-unknown-elf-'
    CC      = PREFIX + 'gcc'
    CXX     = PREFIX + 'g++'
    AS      = PREFIX + 'gcc'
    AR      = PREFIX + 'ar'
    LINK    = PREFIX + 'gcc'
    TARGET_EXT = 'elf'
    SIZE    = PREFIX + 'size'
    OBJDUMP = PREFIX + 'objdump'
    OBJCPY  = PREFIX + 'objcopy'
    STRIP   = PREFIX + 'strip'

    def _gcc_isa_tag():
        gcc = os.path.join(EXEC_PATH, CC) if EXEC_PATH else CC
        if os.name == 'nt' and not gcc.lower().endswith('.exe'):
            gcc_exe = gcc + '.exe'
            if os.path.exists(gcc_exe):
                gcc = gcc_exe
        if not (os.path.exists(gcc) or shutil.which(gcc)):
            return ''
        try:
            version = subprocess.check_output(
                [gcc, '-dumpversion'], stderr=subprocess.STDOUT).decode()
            return '_zifencei' if int(version.strip().split('.')[0]) >= 12 else ''
        except Exception:
            return ''

    # D12p uses a T-Head E907F core with RV32IMafc (single-precision FPU).
    DEVICE  = ' -march=rv32imafc' + _gcc_isa_tag() + \
              ' -mabi=ilp32f -mcmodel=medlow'
    CFLAGS  = DEVICE + ' -ffunction-sections -fdata-sections -fno-common -Wall'
    AFLAGS  = ' -c' + DEVICE + ' -x assembler-with-cpp -D__ASSEMBLY__'
    LFLAGS  = DEVICE + ' -nostartfiles -Wl,--gc-sections ' + \
              '-Wl,-zmax-page-size=1024 -Wl,-Map=rtthread.map -T link.lds'

    if BUILD == 'debug':
        CFLAGS += ' -O0 -g3 -gdwarf-2'
        AFLAGS += ' -g3 -gdwarf-2'
    else:
        CFLAGS += ' -O2 -g'
        AFLAGS += ' -g'

    CXXFLAGS = CFLAGS
    CPATH = ''
    LPATH = ''

DUMP_ACTION = OBJDUMP + ' -D -S $TARGET > rtthread.asm\n'
POST_ACTION = OBJCPY + ' -O binary $TARGET rtthread.bin\n' + SIZE + ' $TARGET\n'

def dist_handle(BSP_ROOT, dist_dir):
    import sys
    sys.path.append(os.path.join(os.path.dirname(BSP_ROOT), 'tools'))
    from sdk_dist import dist_do_building
    dist_do_building(BSP_ROOT, dist_dir)
