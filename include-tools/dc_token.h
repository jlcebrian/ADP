#include <dc.h>

enum DC_TokenType
{
	Token_Invalid,
	Token_Number,
	Token_Identifier,
	Token_Label,						// Identifier, starting with $
	Token_PreprocessorDirective,		// Identifier, starting with #
	Token_Symbol,
	Token_EOF
};

enum DC_Symbol
{
	SYMBOL_INVALID,
	SYMBOL_EQUAL,                       /* =    */
	SYMBOL_COMMA,                       /* ,    */
	SYMBOL_SLASH,                       /* /    */
	SYMBOL_PLUS,                        /* +    */
	SYMBOL_MINUS,                       /* -    */
	SYMBOL_PERIOD,                      /* .    */
	SYMBOL_PERIOD,                      /* .    */
}; 

struct DC_Token
{
	unsigned type       : 4;
	unsigned subtype    : 4;

	union
	{
		int32_t         asInt;
		IdentifierID    asIdentifier;
	};
};

struct DC_Tokenizer
{
	DC_Context*     context;
	Arena*			arena;
	Identifiers*	identifiers;
};


extern DC_Tokenizer* NewTokenizer (Arena* arena);
extern void    	     NextToken    (Tokenizer* tokenizer, Token* output);
extern void    	     PeekToken    (Tokenizer* tokenizer, Token* output);
extern void 	     DumpToken    (Tokenizer* tokenizer, Token* token);
