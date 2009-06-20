<?php

/**
 * Raw Network Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class RawNetworkClass {
	private $_socket;
	
	public function createRawSocket(int $ipProtocol, int $contentProtocol) {
		if ($ipProtocol == PROT_IPv4)
			$socketFamiliy = AF_INET;
		else if ($ipProtocol == PROT_IPv4)
			$socketFamiliy = AF_INET6;
		else
			new Exception("Unknown IP protocol");
			
		$this->_socket = socket_create($socketFamiliy, SOCK_RAW, $contentProtocol);
		
		if (!$this->_socket) {
			throw new Exception(socket_strerror(socket_last_error()));
		}
	}
	
	public function setSendCustomIPHeader() {
		if (!$this->_socket) {
			throw new Exception('Socket not yet opened!');
		}
	}
	
	public function closeSocket() {
		if (is_resource($this->_socket)) {
			socket_close($this->_socket);
			
			$this->_socket = null;
		}
	}
}

?>