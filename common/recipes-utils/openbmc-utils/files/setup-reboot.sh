#!/bin/sh

soc_model=$(cat /etc/soc_model 2>/dev/null)
if [ "$soc_model" = "SOC_MODEL_ASPEED_G7" ]; then
    # No AST2700 mapping for the SRAM reboot-cause handoff used by
    # healthd is established.
    exit 0
fi

sram_reboot="0x1e721204"
if [ "$soc_model" = "SOC_MODEL_ASPEED_G6" ]; then
    sram_reboot="0x10015c04"
fi

# flag for healthd logging reboot cause
val=$(devmem "$sram_reboot" 32 2>/dev/null)
devmem "$sram_reboot" 32 $((val | 0x101)) 2>/dev/null
