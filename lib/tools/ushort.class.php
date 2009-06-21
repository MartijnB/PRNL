<?php

/**
 * UShort Class
 * 
 * PHP Raw Network Library
 * (c) 2009 Kenneth van Hooff & Martijn Bogaard
 */

class UShort {
	private $_value;
	
	public function __construct($value = 0) {
		$this->add(($value & 0xFFFF));
	}
	
	public function add($value) {
		$this->_value += $value % 0x10000;
		
		if ($this->_value > 0xFFFF) {
			$this->_value = $this->_value % 0x10000;
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

?>