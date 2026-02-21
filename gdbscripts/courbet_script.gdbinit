python
import sys, os
path = os.path.join(os.getcwd(), "courbet")
sys.path.insert(0, path)
end

file courbet/bin/ardurover_v462
target remote :1235
add-symbol-file courbet/bin/ardurover_v462
set substitute-path ../../ /root/rooney/Rover-4.6.2/ardupilot/

set pagination off
source courbet/tools/hook_verifier.py
courbet_verify virtuals/ardurover_virtuals.c courbet/rover462/labeled_conf/virtuals.txt
source courbet/tools/timer_interrupt_info.py
    
