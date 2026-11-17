#ifndef UTILS_H
#define UTILS_H

#include "Definitions.h"
#include "BoardState.h"
#include <vector>
#include <string>
#include <regex>

namespace utils {
	using namespace std;
	using namespace defs;
	using namespace bstate;

	const regex MOVE_REGEX{ "([a-h]{1}[1-8]{1}){2}[RNBQ]?" };

	U64 availableMemory();

	Inline vector<string> split(const string& str, const char delim) {
		vector<string> result;
		string elem;
		for (int i = 0; i < str.length(); ++i) {
			if (str[i] == delim) {
				if (elem.length() > 0) {
					result.push_back(elem);
				}
				elem.clear();
				continue;
			}
			elem += str[i];
		}
		result.push_back(elem);
		return result;
	}

	Inline string getMoveSimple(const BoardState& move) {
		string result;

		result += SQUARE_NAMES[move.from];
		result += SQUARE_NAMES[move.to];
		if (move.mvType == PROMO) {
			result += ASCII_PIECES[0][move.promo];
		}

		return result;
	}

	Inline string getMoveUci(const BoardState& move) {
		string result;

		result += SQUARE_NAMES[move.from];
		result += SQUARE_NAMES[move.to];
		if (move.mvType == PROMO) {
			result += ASCII_PIECES[1][move.promo];
		}

		return result;
	}

	Inline bool validMove(string& move) {
		return regex_match(move, MOVE_REGEX);
	}

	Inline bool isValidNumber(const string& str) {
		if (str.empty() || str.find_first_not_of("0123456789") != string::npos) return false;

		try {
			(void)stoi(str);
			return true;
		}
		catch (...) {}

		return false;
	}

	Inline void clean(string& input) {
		input.erase(input.find_last_not_of(" \t\r") + 1);
	}
}

#endif