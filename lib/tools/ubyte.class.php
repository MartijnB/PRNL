<?php

/**
 * UByte Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class UByte {
	private $_value;
	
	public function __construct($value = 0) {
		$this->add(($value & 0xFF));
	}
	
	public function add($value) {
		$this->_value += $value % 0x100;
		
		if ($this->_value > 0xFF) {
			$this->_value = $this->_value % 0x100;
		}
	}
	
	public function subtract($value) {
		$this->_value -= ($value % 0x100);
		
		if ($this->_value < 0)
			$this->_value &= 0xFF;
	}
	
	public function setValue($value) {
		$this->_value = $value % 0xFF;
	}
	
	public function bitAnd($value) {
		$this->_value = $this->_value & ($value & 0xFF);
	}
	
	public function bitOr($value) {
		$this->_value = $this->_value | ($value & 0xFF);
	}
	
	public function bitXOr($value) {
		$this->_value = $this->_value ^ ($value & 0xFF);
	}
	
	public function bitNot() {
		$this->_value = (~$this->_value) & 0xFF;
	}
	
	public function getValue() {
		return $this->_value;
	}
	
	public function __toString() {
		return (string)$this->getValue();
	}
}

?>