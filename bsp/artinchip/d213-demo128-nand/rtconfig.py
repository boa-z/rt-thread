import os
import shutil
import subprocess

# Toolchain options
ARCH        = 'risc-v'
CPU         = 'rv64'
CROSS_TOOL  = 'gcc'

RTT_ROOT = os.getenv('RTT_ROOT') or os.path.normpath(os.path.join(os.getcwd(), '..', '..', '..'))

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

    # Newer RISC-V GCC releases require zifencei to be named explicitly.
    # The Xuantie V2.6.1 toolchain used for the bring-up does not accept it,
    # so select the tag from the actual compiler version.
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

    # D21x uses a T-Head C906 core with RV64GC (IMAFDC).
    DEVICE  = ' -march=rv64imafdc' + _gcc_isa_tag() + \
              ' -mabi=lp64d -mcmodel=medany'
    CFLAGS  = DEVICE + ' -ffunction-sections -fdata-sections -fno-common -Wall'
    AFLAGS  = ' -c' + DEVICE + ' -x assembler-with-cpp -D__ASSEMBLY__'
    LFLAGS  = DEVICE + ' -nostartfiles -Wl,--gc-sections -Wl,-Map=rtthread.map -T link.lds'

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
