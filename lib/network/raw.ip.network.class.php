<?php

/**
 * Raw IP Network Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class RawIPNetwork extends RawNetwork  {
	private $_ipProtocol;
	private $_contentProtocol;
	
	public function createIPSocket($ipProtocol, $contentProtocol) {
		if ($ipProtocol == PROT_IPv4)
			$socketFamiliy = AF_INET;
		else if ($ipProtocol == PROT_IPv6)
			$socketFamiliy = AF_INET6;
		else
			new Exception("Unknown IP protocol");
		
		parent::createRawSocket($socketFamiliy, SOCK_RAW, $contentProtocol);
		
		//force own IP header
		socket_setopt($this->_socket, $this->_ipProtocol, 3, 1); //IP_HDRINCL = 3
		
		$this->_ipProtocol = $ipProtocol;
		$this->_contentProtocol = $contentProtocol;
	}
}

?>