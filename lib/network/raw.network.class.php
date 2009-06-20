<?php

/**
 * Raw Network Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class RawNetwork {
	private $_socket;
	
	private $_ipProtocol;
	private $_contentProtocol;
	
	public function createRawSocket(int $ipProtocol, int $contentProtocol) {
		if ($ipProtocol == PROT_IPv4)
			$socketFamiliy = AF_INET;
		else if ($ipProtocol == PROT_IPv6)
			$socketFamiliy = AF_INET6;
		else
			new Exception("Unknown IP protocol");
			
		$this->_socket = socket_create($socketFamiliy, SOCK_RAW, $contentProtocol);
		
		if (!$this->_socket) {
			throw new Exception(socket_strerror(socket_last_error()));
		}
		
		$this->_ipProtocol = $ipProtocol;
		$this->_contentProtocol = $contentProtocol;
	}
	
	public function setSendCustomIPHeader() {
		if (!$this->_socket) {
			throw new Exception('Socket not yet opened!');
		}
		
		socket_setopt($this->_socket, $this->_ipProtocol, 3, 1); //IP_HDRINCL = 3
	}
	
	public function closeSocket() {
		if (is_resource($this->_socket)) {
			socket_close($this->_socket);
			
			$this->_socket = null;
		}
	}
}

?>