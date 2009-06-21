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
		$this->add($value);
	}
	
	public function add($value) {
		if ($value > 255) {
			$value -= 256;
			$this->add($value);
			return;
		}
		
		if (($this->_value + $value) > 255) {
			$this->_value = ($this->_value + $value) - 256;
		}
		else {
			$this->_value += $value;
		}
	}
	
	public function subtract($value) {
		if ($value > 255) {
			$value -= 256;
			$this->distract($value);
			return;
		}
		
		if (($this->_value - $value) < 0) {
			$this->_value = ($this->_value - $value) + 256;
		}
		else {
			$this->_value -= $value;
		}
	}
	
	public function setValue($value) {
		if ($value > 255) {
			$value -= 256;
			$this->setValue($value);
			return;
		}
		
		$this->_value = $value;
	}
	
	public function bitAnd($value) {
		$this->_value = $this->_value & $value;
	}
	
	public function bitOr($value) {
		$this->_value = $this->_value | $value;
	}
	
	public function bitXOr($value) {
		$this->_value = $this->_value ^ $value;
	}
	
	public function bitNot() {
		$this->_value = (~$this->_value) ^ 0xFFFFFF00;
	}
	
	public function getValue() {
		return $this->_value;
	}
	
	public function __toString() {
		return (string)$this->getValue();
	}
}

?>