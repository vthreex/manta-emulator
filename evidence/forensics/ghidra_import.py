# Ghidra Jython script. Import RAW MIPS:LE:32:default at BFC00000 first.
createLabel(toAddr(0xbfc00000), "reset_candidate", True)
createLabel(toAddr(0xbfc0006c), "cp0_initialize", True)
createLabel(toAddr(0xbfc00c00), "riu_masked_table_apply", True)
createLabel(toAddr(0xbfc00c90), "riu_byte_table_apply", True)
createLabel(toAddr(0xbfc01118), "early_board_initialize", True)
createLabel(toAddr(0xbfc0271c), "copy_second_stage_to_81000000", True)
