<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');


$rawNetworkManager = new RawIPNetwork();
$rawNetworkManager->createIPSocket(PROT_IPv4, PROT_TCP);

while ($packet = $rawNetworkManager->readPacket()) {
	$packet->dumpPacket();
}