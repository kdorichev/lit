#ifndef LIT_CHUNK_HPP
#define LIT_CHUNK_HPP

#include "list.hpp"
#include "lit_value.hpp"
#include "lit_common.hpp"

void* reallocate(void* previous, size_t old_size, size_t new_size);

// Operation codes
enum LitOpCode {
  OP_CONSTANT,
  OP_NEGATE,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_RETURN,
	OP_NOT,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_AND,
	OP_OR,
	OP_PRINT,
	OP_POP,
	OP_JUMP_IF_FALSE,
	OP_JUMP,
	OP_DEFINE_GLOBAL,
	OP_GET_LOCAL,
	OP_SET_LOCAL,
	OP_GET_UPVALUE,
	OP_SET_UPVALUE,
	OP_GET_GLOBAL,
	OP_SET_GLOBAL
};

class LitChunk {
	private:
	  int count;
	  int capacity;
	  int* lines;
	  uint8_t* code;
	  List<LitValue> constants;

	public:
	  LitChunk();
	  ~LitChunk();

	  void write(uint8_t cd, int line);
	  int add_constant(LitValue value);
	  int get_line(int i) { return lines[i]; }
	  int get_count() { return count; }
	  int get_capacity() { return capacity; }
	  uint8_t* get_code() { return code; }
	  List<LitValue>* get_constants() { return &constants; }
};

#endif