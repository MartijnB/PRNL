<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

define('__PRNL_NO_EXTERNAL_MODULES', false);

require_once('../lib/lib.prnl.php');

$m = new Memory();

for ($i = 0; $i < 1000000; $i++) {
	$m->addByte(0xff);
	$m->addShort(0x73);
	$m->setByte($i, 0x00);
	$m->readByte();
}