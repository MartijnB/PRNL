<?php

/**
 * Raw Network Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
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
	
	public function sendPacket(RawPacket $packet) {
		if (!$this->_socket) {
			throw new Exception('Socket not yet opened!');
		}
		
		if (!socket_send($this->_socket, $packet->getRawPacket(), $packet->getPacketLength(), 0)) {
			throw new Exception(socket_strerror(socket_last_error()));
		}
	}
	
	/**
	 * Send a raw packet through the socket
	 *
	 * @param RawPacket $packet
	 */
	public function sendPacketTo(RawPacket $packet, $addr, $port = 0) {
		if (!$this->_socket) {
			throw new Exception('Socket not yet opened!');
		}
		
		if (!socket_sendto($this->_socket, $packet->getRawPacket(), $packet->getPacketLength(), 0, $addr, $port)) {
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