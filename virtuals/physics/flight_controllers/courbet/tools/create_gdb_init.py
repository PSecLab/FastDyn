import sys

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python create_gdb_init.py <bin> <directory>")
        sys.exit(1)

    bin = sys.argv[1]
    directory = sys.argv[2]

    write_out = f'''python
import sys, os
path = os.path.join(os.getcwd(), "courbet")
sys.path.insert(0, path)
end

file courbet/bin/{bin}
target remote :1235
add-symbol-file courbet/bin/{bin}
set substitute-path ../../ /root/rooney/Rover-4.6.2/ardupilot/

set pagination off
source courbet/tools/hook_verifier.py
courbet_verify virtuals/ardurover_virtuals.c courbet/{directory}/labeled_conf/virtuals.txt
source courbet/tools/timer_interrupt_info.py
    '''

    with open(".gdbinit", "w") as f:
        f.write(write_out)