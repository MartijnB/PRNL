<?php

/**
 * Raw Network Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class RawNetwork {
	protected $_socket;
	
	public function createRawSocket($family, $type, $protocol) {
		$this->_socket = socket_create($family, $type, $protocol);
		
		if (!$this->_socket) {
			throw new Exception(socket_strerror(socket_last_error()));
		}
	}
	
	/**
	 * Read a raw packet of the socket
	 *
	 * @param int $length
	 * @return RawPacket
	 */
	public function readPacket($length = 16384) {
		if (!$this->_socket) {
			throw new Exception('Socket not yet opened!');
		}
		
		$buffer = '';
		$readBytes = socket_recv($this->_socket, $buffer, $length, 0);
		
		if ($readBytes > 0) {
			$packet = new RawPacket();
			$packet->setRawPacket($buffer);
			
			return $packet;
		}
		else {
			throw new Exception(socket_strerror(socket_last_error()));
		}
	}
	
	public function sendPacket(IPacket $packet) {
		if (!socket_send($this->_socket, $packet->getRawPacket(), $packet->getLength(), 0)) {
			throw new Exception(socket_strerror(socket_last_error()));
		}
	}
	
	public function closeSocket() {
		if (is_resource($this->_socket)) {
			socket_close($this->_socket);
			
			$this->_socket = null;
		}
	}
	
	public function __destruct() {
		$this->closeSocket();
	}
}

?>