<?php

if (php_sapi_name() != 'cli') {
	die('This script can only run from the commandline!'.PHP_EOL);
}

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');

if ($_SERVER["argc"] != 3)
	die('php '.$_SERVER['argv'][0].' <ip> <port>'.PHP_EOL);

$ip = $_SERVER['argv'][1];
$port = (int)$_SERVER['argv'][2];

$rawNetworkManager = new RawNetwork();

$rawNetworkManager->createRawSocket(PROT_IPv4, PROT_UDP);