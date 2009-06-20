<?php

if (php_sapi_name() != 'cli') {
	die('This script can only run from the commandline!'.PHP_EOL);
}

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');

if ($_SERVER["argc"] != 2)
	die('./script <ip> <port>'.PHP_EOL);
	
var_dump($_SERVER['argv']);