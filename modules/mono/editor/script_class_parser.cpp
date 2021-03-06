/*************************************************************************/
/*  script_class_parser.cpp                                              */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "script_class_parser.h"

#include "core/map.h"
#include "core/os/os.h"

#include "../utils/string_utils.h"

const char *ScriptClassParser::token_names[ScriptClassParser::TK_MAX] = {
	"[",
	"]",
	"{",
	"}",
	"(",
	")",
	".",
	"?",
	":",
	",",
	"Symbol",
	"Identifier",
	"String",
	"Number",
	"<",
	">",
	"EOF",
	"Error"
};

String ScriptClassParser::get_token_name(ScriptClassParser::Token p_token) {

	ERR_FAIL_INDEX_V(p_token, TK_MAX, "<error>");
	return token_names[p_token];
}

ScriptClassParser::Token ScriptClassParser::get_token() {

	while (true) {
		switch (code[idx]) {
			case '\n': {
				line++;
				idx++;
				break;
			};
			case 0: {
				return TK_EOF;
			} break;
			case '{': {
				idx++;
				return TK_CURLY_BRACKET_OPEN;
			};
			case '}': {
				idx++;
				return TK_CURLY_BRACKET_CLOSE;
			};
			case '[': {
				idx++;
				return TK_BRACKET_OPEN;
			};
			case ']': {
				idx++;
				return TK_BRACKET_CLOSE;
			};
			case '(': {
				idx++;
				return TK_PARENS_OPEN;
			};
			case ')': {
				idx++;
				return TK_PARENS_CLOSE;
			};
			case '<': {
				idx++;
				return TK_OP_LESS;
			};
			case '>': {
				idx++;
				return TK_OP_GREATER;
			};
			case ':': {
				idx++;
				return TK_COLON;
			};
			case ',': {
				idx++;
				return TK_COMMA;
			};
			case '.': {
				idx++;
				return TK_PERIOD;
			};
			case '?': {
				idx++;
				return TK_QUESTION;
			};
			case '#': {
				//compiler directive
				while (code[idx] != '\n' && code[idx] != 0) {
					idx++;
				}
				continue;
			} break;
			case '/': {
				switch (code[idx + 1]) {
					case '*': { // block comment
						idx += 2;
						while (true) {
							if (code[idx] == 0) {
								error_str = "Line: " + String::num_int64(line) + " - Unterminated comment";
								error = true;
								return TK_ERROR;
							} else if (code[idx] == '*' && code[idx + 1] == '/') {
								idx += 2;
								break;
							} else if (code[idx] == '\n') {
								line++;
							}

							idx++;
						}

					} break;
					case '/': { // line comment skip
						while (code[idx] != '\n' && code[idx] != 0) {
							idx++;
						}

					} break;
					default: {
						value = "/";
						idx++;
						return TK_SYMBOL;
					}
				}

				continue; // a comment
			} break;
			case '\'':
			case '"': {
				bool verbatim = idx != 0 && code[idx - 1] == '@';

				CharType begin_str = code[idx];
				idx++;
				String tk_string = String();
				while (true) {
					if (code[idx] == 0) {
						error_str = "Line: " + String::num_int64(line) + " - Unterminated String";
						error = true;
						return TK_ERROR;
					} else if (code[idx] == begin_str) {
						if (verbatim && code[idx + 1] == '"') { // `""` is verbatim string's `\"`
							idx += 2; // skip next `"` as well
							continue;
						}

						idx += 1;
						break;
					} else if (code[idx] == '\\' && !verbatim) {
						//escaped characters...
						idx++;
						CharType next = code[idx];
						if (next == 0) {
							error_str = "Line: " + String::num_int64(line) + " - Unterminated String";
							error = true;
							return TK_ERROR;
						}
						CharType res = 0;

						switch (next) {
							case 'b': res = 8; break;
							case 't': res = 9; break;
							case 'n': res = 10; break;
							case 'f': res = 12; break;
							case 'r':
								res = 13;
								break;
							case '\"': res = '\"'; break;
							case '\\':
								res = '\\';
								break;
							default: {
								res = next;
							} break;
						}

						tk_string += res;

					} else {
						if (code[idx] == '\n')
							line++;
						tk_string += code[idx];
					}
					idx++;
				}

				value = tk_string;

				return TK_STRING;
			} break;
			default: {
				if (code[idx] <= 32) {
					idx++;
					break;
				}

				if ((code[idx] >= 33 && code[idx] <= 39) || (code[idx] >= 42 && code[idx] <= 47) || (code[idx] >= 58 && code[idx] <= 62) || (code[idx] >= 91 && code[idx] <= 94) || code[idx] == 96 || (code[idx] >= 123 && code[idx] <= 127)) {
					value = String::chr(code[idx]);
					idx++;
					return TK_SYMBOL;
				}

				if (code[idx] == '-' || (code[idx] >= '0' && code[idx] <= '9')) {
					//a number
					const CharType *rptr;
					double number = String::to_double(&code[idx], &rptr);
					idx += (rptr - &code[idx]);
					value = number;
					return TK_NUMBER;

				} else if ((code[idx] == '@' && code[idx + 1] != '"') || code[idx] == '_' || (code[idx] >= 'A' && code[idx] <= 'Z') || (code[idx] >= 'a' && code[idx] <= 'z') || code[idx] > 127) {
					String id;

					id += code[idx];
					idx++;

					while (code[idx] == '_' || (code[idx] >= 'A' && code[idx] <= 'Z') || (code[idx] >= 'a' && code[idx] <= 'z') || (code[idx] >= '0' && code[idx] <= '9') || code[idx] > 127) {
						id += code[idx];
						idx++;
					}

					value = id;
					return TK_IDENTIFIER;
				} else if (code[idx] == '@' && code[idx + 1] == '"') {
					// begin of verbatim string
					idx++;
				} else {
					error_str = "Line: " + String::num_int64(line) + " - Unexpected character.";
					error = true;
					return TK_ERROR;
				}
			}
		}
	}
}

// If the next token is the compare token, returns true. Doesn't consume any tokens.
bool ScriptClassParser::_try_parse(Token compare) {
	int temp_idx = idx;
	int temp_line = line;

	Token tk = get_token();

	idx = temp_idx;
	line = temp_line;

	return compare == tk;
}

// If the tokens are the compare tokens, returns true. Doesn't consume any tokens.
bool ScriptClassParser::_try_parse(Vector<Token> &compare) {
	int temp_idx = idx;
	int temp_line = line;

	for (int i = 0; i < compare.size(); i++) {
		Token tk = get_token();

		if (compare[i] != tk) {
			idx = temp_idx;
			line = temp_line;

			return false;
		}
	}

	idx = temp_idx;
	line = temp_line;

	return true;
}

Error ScriptClassParser::_skip_generic_type_params() {

	Token tk;

	while (true) {
		tk = get_token();

		// Parses type first:

		// Type could be a tuple type and start with a parens
		if (tk == TK_PARENS_OPEN) {
			Error err = _skip_tuple_type_params();
			if (err)
				return err;
			tk = get_token();
		} else if (tk == TK_IDENTIFIER) { // Could be a regular type
			tk = get_token();

			// Could be a type from another namespace/class
			while (tk == TK_PERIOD) {

				// Should be another namespace/class
				tk = get_token();

				if (tk != TK_IDENTIFIER) {
					error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + ", found: " + get_token_name(tk);
					error = true;
					return ERR_PARSE_ERROR;
				}

				tk = get_token();
			}

			// Type could be a generic type, such as IList<int>
			if (tk == TK_OP_LESS) {
				Error err = _skip_generic_type_params();
				if (err)
					return err;
				tk = get_token();
			}

			// Skip Array declarations (i.e. List<T[]> or List<int?[]>)
			while (tk == TK_BRACKET_OPEN) {
				// Next token MUST be ]
				tk = get_token();
				if (tk != TK_BRACKET_CLOSE) {
					error_str = "Line: " + String::num_int64(line) + " - Expected ] after [. But found " + get_token_name(tk) + " next.";
					error = true;
					return ERR_PARSE_ERROR;
				}
				tk = get_token();
			}

			// Type specifications can end with "?" to denote nullable types, such as int?
			if (tk == TK_QUESTION) {
				tk = get_token();
			}
		}

		// Type is parsed now.
		if (tk == TK_OP_GREATER) {
			// Type specifications can end with "?" to denote nullable types, such as List<int>?
			if (_try_parse(TK_QUESTION)) {
				tk = get_token();
			}

			return OK;
		} else if (tk == TK_COMMA) {
			// This is okay, but we're still looking for the end of this generic type, so continue on
		} else {
			error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
			error = true;
			return ERR_PARSE_ERROR;
		}
	}
}

Error ScriptClassParser::_skip_tuple_type_params() {

	Token tk;

	while (true) {
		tk = get_token();

		// Parses type first:

		// Type could be a tuple type and start with a parens
		if (tk == TK_PARENS_OPEN) {
			Error err = _skip_tuple_type_params();
			if (err)
				return err;
			tk = get_token();
		} else if (tk == TK_IDENTIFIER) { // Could be a regular type
			tk = get_token();

			// Could be a type from another namespace/class
			while (tk == TK_PERIOD) {

				// Should be another namespace/class
				tk = get_token();

				if (tk != TK_IDENTIFIER) {
					error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + ", found: " + get_token_name(tk);
					error = true;
					return ERR_PARSE_ERROR;
				}

				tk = get_token();
			}

			// Type could be a generic type, such as IList<int>
			if (tk == TK_OP_LESS) {
				Error err = _skip_generic_type_params();
				if (err)
					return err;
				tk = get_token();
			}

			// Skip Array declarations (i.e. List<T[]> or List<int?[]>)
			while (tk == TK_BRACKET_OPEN) {
				// Next token MUST be ]
				tk = get_token();
				if (tk != TK_BRACKET_CLOSE) {
					error_str = "Line: " + String::num_int64(line) + " - Expected ] after [. But found " + get_token_name(tk) + " next.";
					error = true;
					return ERR_PARSE_ERROR;
				}
				tk = get_token();
			}

			// Type specifications can end with "?" to denote nullable types, such as int?
			if (tk == TK_QUESTION) {
				tk = get_token();
			}
		} else {
			error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
			error = true;
			return ERR_PARSE_ERROR;
		}

		// Type is parsed now.

		// After the type, you can give the tuple a name, such as (int? a, int b)
		if (tk == TK_IDENTIFIER) {
			tk = get_token();
		}

		if (tk == TK_PARENS_CLOSE) {
			// Type specifications can end with "?" to denote nullable types, such as List<int>?
			if (_try_parse(TK_QUESTION)) {
				tk = get_token();
			}

			return OK;
		} else if (tk == TK_COMMA) {
			continue;
		} else {
			error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
			error = true;
			return ERR_PARSE_ERROR;
		}
	}
}

Error ScriptClassParser::_parse_type_full_name(String &r_full_name) {

	Token tk = get_token();

	if (tk != TK_IDENTIFIER) {
		error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + ", found: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}

	r_full_name += String(value);

	if (_try_parse(TK_OP_LESS)) {
		tk = get_token();

		// We don't mind if the base is generic, but we skip it any ways since this information is not needed
		Error err = _skip_generic_type_params();
		if (err)
			return err;
	}

	if (code[idx] != '.') // We only want to take the next token if it's a period
		return OK;

	tk = get_token();

	CRASH_COND(tk != TK_PERIOD); // Assertion

	r_full_name += ".";

	return _parse_type_full_name(r_full_name);
}

Error ScriptClassParser::_parse_class_base(Vector<String> &r_base) {

	String name;

	Error err = _parse_type_full_name(name);
	if (err)
		return err;

	Token tk = get_token();

	if (tk == TK_COMMA) {
		err = _parse_class_base(r_base);
		if (err)
			return err;
	} else if (tk == TK_IDENTIFIER && String(value) == "where") {
		err = _parse_type_constraints();
		if (err) {
			return err;
		}

		// An open curly bracket was parsed by _parse_type_constraints, so we can exit
	} else if (tk == TK_CURLY_BRACKET_OPEN) {
		// we are finished when we hit the open curly bracket
	} else {
		error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}

	r_base.push_back(name);

	return OK;
}

Error ScriptClassParser::_parse_type_constraints() {
	// Get name
	Token tk = get_token();
	if (tk != TK_IDENTIFIER) {
		error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}

	// Get colon
	tk = get_token();
	if (tk != TK_COLON) {
		error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}

	while (true) {
		tk = get_token();
		// Get type
		if (tk == TK_IDENTIFIER) {
			if (String(value) == "where") {
				return _parse_type_constraints();
			}

			tk = get_token();
			while (tk == TK_PERIOD) {
				tk = get_token();

				if (tk != TK_IDENTIFIER) {
					error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + ", found: " + get_token_name(tk);
					error = true;
					return ERR_PARSE_ERROR;
				}

				tk = get_token();
			}

			// Type could be a generic type
			if (tk == TK_OP_LESS) {
				Error err = _skip_generic_type_params();
				if (err)
					return err;
				tk = get_token();
			}

			// Type can end in () (like the type constraint new())
			if (tk == TK_PARENS_OPEN) {
				tk = get_token();
				if (tk != TK_PARENS_CLOSE) {
					error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
					error = true;
					return ERR_PARSE_ERROR;
				}
				tk = get_token();
			}
		} else {
			error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + ", found: " + get_token_name(tk);
			error = true;
			return ERR_PARSE_ERROR;
		}

		// Parsed type at this point

		if (tk == TK_COMMA) {
			continue;
		} else if (tk == TK_IDENTIFIER && String(value) == "where") {
			return _parse_type_constraints();
		} else if (tk == TK_CURLY_BRACKET_OPEN) {
			return OK;
		} else {
			error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
			error = true;
			return ERR_PARSE_ERROR;
		}
	}
}

Error ScriptClassParser::_parse_namespace_name(String &r_name, int &r_curly_stack) {

	Token tk = get_token();

	if (tk == TK_IDENTIFIER) {
		r_name += String(value);
	} else {
		error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}

	tk = get_token();

	if (tk == TK_PERIOD) {
		r_name += ".";
		return _parse_namespace_name(r_name, r_curly_stack);
	} else if (tk == TK_CURLY_BRACKET_OPEN) {
		r_curly_stack++;
		return OK;
	} else {
		error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
		error = true;
		return ERR_PARSE_ERROR;
	}
}

Error ScriptClassParser::parse(const String &p_code) {

	code = p_code;
	idx = 0;
	line = 1;
	error_str = String();
	error = false;
	value = Variant();
	classes.clear();

	Token tk = get_token();

	Map<int, NameDecl> name_stack;
	int curly_stack = 0;
	int type_curly_stack = 0;

	while (!error && tk != TK_EOF) {
		// It's possible to use "class" and "struct" as type constraints instead of type declarations, so consume those first
		if (tk == TK_IDENTIFIER && String(value) == "where") {
			// However, it's also possible that if we see a "where", it isn't a type constraint
			Vector<Token> tokens;
			tokens.push_back(TK_IDENTIFIER);
			tokens.push_back(TK_COLON);
			tokens.push_back(TK_IDENTIFIER);

			if (_try_parse(tokens)) {
				Error err = _parse_type_constraints();
				if (err)
					return err;
				tk = get_token();
			}
		} else if (tk == TK_IDENTIFIER && String(value) == "class") {
			tk = get_token();

			if (tk == TK_IDENTIFIER) {
				String name = value;
				int at_level = type_curly_stack;

				ClassDecl class_decl;

				for (Map<int, NameDecl>::Element *E = name_stack.front(); E; E = E->next()) {
					const NameDecl &name_decl = E->value();

					if (name_decl.type == NameDecl::NAMESPACE_DECL) {
						if (E != name_stack.front())
							class_decl.namespace_ += ".";
						class_decl.namespace_ += name_decl.name;
					} else {
						class_decl.name += name_decl.name + ".";
					}
				}

				class_decl.name += name;
				class_decl.nested = type_curly_stack > 0;

				bool generic = false;

				while (true) {
					tk = get_token();

					if (tk == TK_COLON) {
						Error err = _parse_class_base(class_decl.base);
						if (err)
							return err;

						curly_stack++;
						type_curly_stack++;

						break;
					} else if (tk == TK_CURLY_BRACKET_OPEN) {
						curly_stack++;
						type_curly_stack++;
						break;
					} else if (tk == TK_OP_LESS && !generic) {
						generic = true;

						Error err = _skip_generic_type_params();
						if (err)
							return err;
					} else if (tk == TK_IDENTIFIER && String(value) == "where") {
						Error err = _parse_type_constraints();
						if (err) {
							return err;
						}

						// An open curly bracket was parsed by _parse_type_constraints, so we can exit
						curly_stack++;
						type_curly_stack++;
						break;
					} else {
						error_str = "Line: " + String::num_int64(line) + " - Unexpected token: " + get_token_name(tk);
						error = true;
						return ERR_PARSE_ERROR;
					}
				}

				NameDecl name_decl;
				name_decl.name = name;
				name_decl.type = NameDecl::CLASS_DECL;
				name_stack[at_level] = name_decl;

				if (!generic) { // no generics, thanks
					classes.push_back(class_decl);
				} else if (OS::get_singleton()->is_stdout_verbose()) {
					String full_name = class_decl.namespace_;
					if (full_name.length())
						full_name += ".";
					full_name += class_decl.name;
					OS::get_singleton()->print("Ignoring generic class declaration: %s\n", class_decl.name.utf8().get_data());
				}
			}
		} else if (tk == TK_IDENTIFIER && String(value) == "struct") {
			String name;
			int at_level = type_curly_stack;
			while (true) {
				tk = get_token();
				if (tk == TK_IDENTIFIER && name.empty()) {
					name = String(value);
				} else if (tk == TK_CURLY_BRACKET_OPEN) {
					if (name.empty()) {
						error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_IDENTIFIER) + " after keyword `struct`, found " + get_token_name(TK_CURLY_BRACKET_OPEN);
						error = true;
						return ERR_PARSE_ERROR;
					}

					curly_stack++;
					type_curly_stack++;
					break;
				} else if (tk == TK_EOF) {
					error_str = "Line: " + String::num_int64(line) + " - Expected " + get_token_name(TK_CURLY_BRACKET_OPEN) + " after struct decl, found " + get_token_name(TK_EOF);
					error = true;
					return ERR_PARSE_ERROR;
				}
			}

			NameDecl name_decl;
			name_decl.name = name;
			name_decl.type = NameDecl::STRUCT_DECL;
			name_stack[at_level] = name_decl;
		} else if (tk == TK_IDENTIFIER && String(value) == "namespace") {
			if (type_curly_stack > 0) {
				error_str = "Line: " + String::num_int64(line) + " - Found namespace nested inside type.";
				error = true;
				return ERR_PARSE_ERROR;
			}

			String name;
			int at_level = curly_stack;

			Error err = _parse_namespace_name(name, curly_stack);
			if (err)
				return err;

			NameDecl name_decl;
			name_decl.name = name;
			name_decl.type = NameDecl::NAMESPACE_DECL;
			name_stack[at_level] = name_decl;
		} else if (tk == TK_CURLY_BRACKET_OPEN) {
			curly_stack++;
		} else if (tk == TK_CURLY_BRACKET_CLOSE) {
			curly_stack--;
			if (name_stack.has(curly_stack)) {
				if (name_stack[curly_stack].type != NameDecl::NAMESPACE_DECL)
					type_curly_stack--;
				name_stack.erase(curly_stack);
			}
		}

		tk = get_token();
	}

	if (!error && tk == TK_EOF && curly_stack > 0) {
		error_str = "Reached EOF with missing close curly brackets.";
		error = true;
	}

	if (error)
		return ERR_PARSE_ERROR;

	return OK;
}

Error ScriptClassParser::parse_file(const String &p_filepath) {

	String source;

	Error ferr = read_all_file_utf8(p_filepath, source);
	if (ferr != OK) {
		if (ferr == ERR_INVALID_DATA) {
			ERR_EXPLAIN("File '" + p_filepath + "' contains invalid unicode (utf-8), so it was not loaded. Please ensure that scripts are saved in valid utf-8 unicode.");
		}
		ERR_FAIL_V(ferr);
	}

	return parse(source);
}

String ScriptClassParser::get_error() {
	return error_str;
}

Vector<ScriptClassParser::ClassDecl> ScriptClassParser::get_classes() {
	return classes;
}
