	/* Functions marked ITCM_CODE (common/placement.h) run from zero-wait
	   ITCM. They are stored in QSPI flash and copied at boot
	   (common/daisy_main.cpp). The first 8 bytes are skipped so that no
	   function sits at address 0. */
	.itcm_text :
	{
		. = ALIGN(8);
		. = . + 8;
		_itcm_text_start = .;
		*(.itcm_text .itcm_text.*)
		. = ALIGN(4);
		_itcm_text_end = .;
	} > ITCMRAM AT > QSPIFLASH
	_sitcm_text = LOADADDR(.itcm_text) + (_itcm_text_start - ADDR(.itcm_text));
