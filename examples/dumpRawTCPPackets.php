<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');


$rawNetworkManager = new RawIPNetwork();
$rawNetworkManager->createIPSocket(PROT_IPv4, PROT_TCP);

while ($packet = $rawNetworkManager->readPacket()) {
	printf("%s:%i -> %s:%i L:%i TTL: %i IDS: %i OFS: %i\n", $packet->getSrcIP(), 0, $packet->getDstIP(), 0, $packet->getLength(), $packet->getTTL(), $packet->getIdSequence(), $packet->getOffset());
	$packet->dumpPacket();
}