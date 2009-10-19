<?php

/**
 * UShort Class
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

class UShort {
	private $_value;
	
	public function __construct($value = 0) {
		$this->add(($value & 0xFFFF));
	}
	
	public function add($value) {
		$this->_value += $value % 0x10000;
		
		if ($this->_value > 0xFFFF) {
			$this->_value = $this->_value % 0xFFFF;
		}
	}
	
	public function subtract($value) {
		$this->_value -= ($value % 0x10000);
		
		if ($this->_value < 0)
			$this->_value &= 0xFFFF;
	}
	
	public function setValue($value) {
		$this->_value = $value % 0xFFFF;
	}
	
	public function bitAnd($value) {
		$this->_value = $this->_value & ($value & 0xFFFF);
	}
	
	public function bitOr($value) {
		$this->_value = $this->_value | ($value & 0xFFFF);
	}
	
	public function bitXOr($value) {
		$this->_value = $this->_value ^ ($value & 0xFFFF);
	}
	
	public function bitNot() {
		$this->_value = (~$this->_value) & 0xFFFF;
	}
	
	public function getValue() {
		return $this->_value;
	}
	
	public function __toString() {
		return (string)$this->getValue();
	}
}