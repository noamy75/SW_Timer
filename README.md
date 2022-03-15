# SW_Timer

The implementataion allows scheduling up to 10 simultaneous SW timer instances, based on a single HW timer module.
The HW timer is connected to the CPU data bus, and has memory-mapped registers.
The HW timer is implemented by a free running 32-bit counter block, counting up at frequency of 1MHz.
