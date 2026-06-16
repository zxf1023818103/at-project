# km0_trace.tcl — single-step trace of KM0's MSP return slot 0x8ffc0 to find the
# instruction that corrupts it (writes the ROM garbage 0x498b) during the scheduler-start
# crash. Server-side loop (runs inside OpenOCD) so PC/memory reads are reliable, unlike a
# telnet step+reg loop.
#
# Usage (KM0 spinning in the g_km0_go gate):
#   source <this>
#   km0_trace <mm_unlock_restore_addr> 0x0008ffc4 <g_km0_go_addr> 40
# It releases the gate, walks mm_unlock_restore breakpoint hits to the one whose entry
# SP == 0x8ffc4 (the crashing call), then prints (PC, [0x8ffc0]) for <n> single-steps.

proc _u {dictval key} { return [dict get $dictval $key] }

# Walk to the <hitn>th mm_unlock_restore hit, then print PC+SP for each of <nsteps> single
# steps (full unwind trace). Shows where a return jumps to the ROM addr that dead-loops.
proc km0_tracePC {mmaddr hitn goaddr nsteps} {
    halt
    catch { rbp $mmaddr }
    bp $mmaddr 2 hw
    mww $goaddr 1
    for {set k 1} {$k <= $hitn} {incr k} {
        resume
        wait_halt 3000
    }
    catch { rbp $mmaddr }
    echo "=== full PC/SP trace from hit $hitn ==="
    for {set i 0} {$i < $nsteps} {incr i} {
        set pc [_u [get_reg pc] pc]
        set sp [_u [get_reg sp] sp]
        echo "[format %3d $i] pc=[format 0x%08x $pc] sp=[format 0x%08x $sp]"
        if {$pc == 0x000049a2} { echo ">>> reached crash loop"; break }
        step
    }
    echo "=== done ==="
}

# Walk to the <hitn>th mm_unlock_restore hit, then single-step <nsteps>, printing every
# change to 0x8ffc0 with the PC of the store that caused it. Stops at the 0x49a2 crash.
proc km0_trace2 {mmaddr hitn goaddr nsteps} {
    halt
    catch { rbp $mmaddr }
    bp $mmaddr 2 hw
    mww $goaddr 1
    for {set k 1} {$k <= $hitn} {incr k} {
        resume
        wait_halt 3000
        echo "hit $k sp=[format 0x%08x [_u [get_reg sp] sp]]"
    }
    catch { rbp $mmaddr }
    echo "=== tracing from hit $hitn ==="
    set prev [lindex [read_memory 0x0008ffc0 32 1] 0]
    echo "start 0x8ffc0=[format 0x%08x $prev]"
    for {set i 0} {$i < $nsteps} {incr i} {
        set pcb [_u [get_reg pc] pc]
        step
        set v [lindex [read_memory 0x0008ffc0 32 1] 0]
        if {$v != $prev} {
            echo "[format %3d $i] store@[format 0x%08x $pcb]: 0x8ffc0 [format 0x%08x $prev] -> [format 0x%08x $v]"
            set prev $v
        }
        set pcnow [_u [get_reg pc] pc]
        if {$pcnow == 0x000049a2} { echo ">>> CRASH pc=0x49a2 at step $i (last store culprit above)"; break }
    }
    echo "=== done ==="
}

proc km0_trace {mmaddr targetsp goaddr nsteps} {
    halt
    catch { rbp $mmaddr }
    bp $mmaddr 2 hw
    mww $goaddr 1            ;# release the gate
    set found 0
    set sp 0
    for {set k 0} {$k < 30} {incr k} {
        resume
        wait_halt 3000
        set sp [_u [get_reg sp] sp]
        if {$sp == $targetsp} { set found 1; break }
    }
    catch { rbp $mmaddr }
    if {!$found} { echo "TARGET SP NOT HIT (last sp=$sp)"; return }
    echo "=== crashing mm_unlock_restore: sp=[format 0x%08x $sp] ==="
    set prev 0
    for {set i 0} {$i < $nsteps} {incr i} {
        set pc [_u [get_reg pc] pc]
        set v  [lindex [read_memory 0x0008ffc0 32 1] 0]
        set mark ""
        if {$i > 0 && $v != $prev} { set mark "   <<< 0x8ffc0 CHANGED" }
        echo "[format %2d $i]  pc=[format 0x%08x $pc]  8ffc0=[format 0x%08x $v]$mark"
        set prev $v
        step
    }
    echo "=== done ==="
}
