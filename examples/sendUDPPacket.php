<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');

if ($_SERVER["argc"] != 3)
	die('php '.$_SERVER['argv'][0].' <ip> <port>'.PHP_EOL);

$ip = $_SERVER['argv'][1];
$port = (int)$_SERVER['argv'][2];

$rawNetworkManager = new RawIPNetwork();						// Create new IPNetwork
$rawNetworkManager->createIPSocket(PROT_IPv4, PROT_UDP);		// Create new IP socket (IPv4 - UDP)

$rawIPv4Package = new IPv4ProtocolPacket();						// Create new IPv4 Packet
$rawIPv4Package->setLength(0);									// Set the length 0, we will calculate it later
$rawIPv4Package->setIdSequence(1);								// Set the Sequence
$rawIPv4Package->setOffset(0);									// Set the Offset
$rawIPv4Package->setTTL(255);									// Set the Time To Live
$rawIPv4Package->setProtocol(PROT_UDP);							// Set protocol to UDP (17)
$rawIPv4Package->setChecksum(0);								// Set the checksum 0, we will calcualte it later
$rawIPv4Package->setSrcIP("127.0.0.1");
$rawIPv4Package->setDstIP($ip);

$rawUDPPackage = new UDPProtocolPacket();						// Create new UDP Packet
$rawUDPPackage->setSrcPort($port);								// Set Source port
$rawUDPPackage->setDstPort($port);								// Set Destination port
$rawUDPPackage->setLength(0);									// Set the length 0, we will calculate it later
$rawUDPPackage->setChecksum(0);									// Set the checksum 0, we will calcualte it later
$rawUDPPackage->setData("Hello World");							// Set the data

print "IP package: ";
print $rawIPv4Package->dumpPacket() . "\n\n";					// Show the IPv4 Package

$rawIPv4Package->setData($rawUDPPackage);						// Add the UDP Package to the IPv4 Package
$rawIPv4Package->completePacket();								// Complete the package (calculate length + checksum); Is optional since the send call completes the package too.

print "UDP package: ";
print $rawUDPPackage->dumpPacket() . "\n\n";					// Show the UDP Package

print "Full package: ";
print $rawIPv4Package->dumpPacket() . "\n\n";					// Show the full (IPv4 + UDP Package)

$rawNetworkManager->sendPacket($rawIPv4Package);				// Send the package

?>