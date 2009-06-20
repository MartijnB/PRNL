<?php

if (php_sapi_name() != 'cli') {
	die('This script can only run from the commandline!'.PHP_EOL);
}

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');


$rawNetworkManager = new RawIPNetwork();
$rawNetworkManager->createRawIPSocket(PROT_IPv4, PROT_UDP);

while ($packet = $rawNetworkManager->readPacket()) {
	$packet->dumpPacket();
}