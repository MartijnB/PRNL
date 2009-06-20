<?php

if (php_sapi_name() != 'cli') {
	die('This script can only run from the commandline!'.PHP_EOL);
}

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');


$rawNetworkManager = new RawNetwork();
$rawNetworkManager->createRawSocket(PROT_IPv4, PROT_IPv4);

while ($packet = $rawNetworkManager->readPacket()) {
	$packet->dumpPacket();
}