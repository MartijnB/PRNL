<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');

if ($_SERVER["argc"] != 3)
	die('php '.$_SERVER['argv'][0].' <ip> <port>'.PHP_EOL);

$ip = $_SERVER['argv'][1];
$port = (int)$_SERVER['argv'][2];

$rawNetworkManager = new RawIPNetwork();						// Create new IPNetwork
$rawNetworkManager->createIPSocket(PROT_IPv4, PROT_TCP);		// Create new IP socket (IPv4 - TCP)

$rawIPv4Package = new IPv4ProtocolPacket();						// Create new IPv4 Packet
$rawIPv4Package->setIdSequence(0);								// Set the Sequence
$rawIPv4Package->setOffset(0);									// Set the Offset
$rawIPv4Package->setTTL(255);									// Set the Time To Live
$rawIPv4Package->setProtocol(PROT_TCP);							// Set protocol to UDP (17)
$rawIPv4Package->setSrcIP("127.0.0.1");
$rawIPv4Package->setDstIP($ip);

$rawTCPPackage = new TCPProtocolPacket();						// Create new TCP Packet
$rawTCPPackage->setSrcPort($port);								// Set Source port
$rawTCPPackage->setDstPort($port);								// Set Destination port
$rawTCPPackage->setFlags(ITCP::FLAG_SYN);
$rawTCPPackage->setData("Hello World!");						// Set the data

print "IP package: ";
print $rawIPv4Package->dumpPacket() . "\n\n";					// Show the IPv4 Package

$rawIPv4Package->setData($rawTCPPackage);						// Add the TCP Package to the IPv4 Package
$rawIPv4Package->completePacket();								// Complete the package (calculate length + checksum); Is optional since the send call completes the package too.

print "TCP package: ";
print $rawTCPPackage->dumpPacket() . "\n\n";					// Show the TCP Package

print "Full package: ";
print $rawIPv4Package->dumpPacket() . "\n\n";					// Show the full (IPv4 + TCP Package)

$rawNetworkManager->sendPacket($rawIPv4Package);				// Send the package

?>