<?php

chdir(dirname(__FILE__)); //change working dir to the script dir

require_once('../lib/lib.prnl.php');

if ($_SERVER["argc"] != 3)
	die('php '.$_SERVER['argv'][0].' <ip> <port>'.PHP_EOL);

$ip = $_SERVER['argv'][1];
$port = (int)$_SERVER['argv'][2];

$rawNetworkManager = new RawIPNetwork();
$rawNetworkManager->createIPSocket(PROT_IPv4, PROT_UDP);

$rawUDPPackage = new UDPProtocolPacket();
$rawUDPPackage->setSrcPort(54321);
$rawUDPPackage->setDstPort(12345);
$rawUDPPackage->setLength(IUDP::HEADER_SIZE + 11);
$rawUDPPackage->setChecksum(0);
$rawUDPPackage->setData("Hello World");

$rawIPv4Package = new IPv4ProtocolPacket();
$rawIPv4Package->setLength(IIPv4::HEADER_SIZE + $rawUDPPackage->getLength());
$rawIPv4Package->setIdSequence(54321);
$rawIPv4Package->setOffset(0);
$rawIPv4Package->setTTL(255);
$rawIPv4Package->setProtocol(PROT_UDP);
$rawIPv4Package->setChecksum(0);
$rawIPv4Package->setSrcIP("192.168.1.1");
$rawIPv4Package->setDstIP("192.168.1.100");
$rawIPv4Package->setData($rawUDPPackage->getPacket());
$rawIPv4Package->calculateChecksum();

$rawNetworkManager->sendPacket($rawIPv4Package);