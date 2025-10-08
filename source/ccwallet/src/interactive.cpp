/*
 * CredaCash (TM) cryptocurrency and blockchain
 *
 * Copyright (C) 2015-2025 Creda Foundation, Inc., or its contributors
 *
 * interactive.cpp
*/

#include "ccwallet.h"
#include "interactive.h"
#include "jsonrpc.h"
#include "walletdb.hpp"

#include <amounts.h>
#include <jsoncpp/json/json.h>

static bool add_quotes(const string& p)
{
	// add double quotes unless the param is true, false, null,
	// or a base10 integer, decimal number or floating point number

	if (!p.length())
		return true;

	if (p == "true" || p == "false" || p == "null")
		return false;

	bool decimal = false;
	bool exp = false;

	for (unsigned j = 0; j < p.length(); ++j)
	{
		auto c = p[j];

		if (j == 0 && c == '-')
			continue;

		if (c == '.' && !decimal && !exp)
		{
			decimal = true;

			continue;
		}

		if (j && (c == 'e' || c == 'E') && !exp)
		{
			exp = true;

			if (j + 1 == p.length())
				return true;

			if (p[j + 1] == '+' || p[j + 1] == '-')
				++j;

			if (j + 1 == p.length())
				return true;

			continue;
		}

		if (c < '0' || c > '9')
			return true;
	}

	return false;
}

string command_line_to_json()
{
	string json;

	if (!g_params.config_options->count("command"))
		return json;

	auto cmd = (*g_params.config_options)["command"].as<vector<string>>();

	if (cmd[0] == "stop" || cmd[0] == "STOP")
		return "stop";

	json = "{\"method\":\"";
	json += cmd[0];
	json += "\",\"params\":[";
	for (unsigned i = 1; i < cmd.size(); ++i)
	{
		if (i > 1)
			json += ",";

		auto p = cmd[i];
		auto plen = p.length();

		if (p[0] == '\'' && p[plen-1] == '\'')
		{
			p.erase(plen-1);
			p.erase(0, 1);
		}

		auto quote = add_quotes(p);

		if (quote)
			json += "\"";

		json += p;

		if (quote)
			json += "\"";
	}

	json += "]}";

	return json;
}

static bool is_term_char(char c)
{
	return (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',' || c == ' ' || c == 0);
}

static string input_to_json(const string& input)
{
	auto cstr = input.c_str();
	string json;
	string token;
	unsigned ntokens = 0;
	bool tstart = true;
	bool tend = false;
	bool quoted = false;
	bool escaped = false;
	bool needs_comma = false;

	for (unsigned i = 0; i <= input.length(); ++i)
	{
		auto c = cstr[i];

		//cerr << "input parse ntokens " << ntokens << " needs_comma " << needs_comma << " tstart " << tstart << " tend " << tend << " quoted " << quoted << " escaped " << escaped << " pos " << i << " char " << (unsigned)c << " = '" << c << "' token '" << token << "' json " << json << endl;

		if (c && c < ' ')
			continue;						// ignore all control chars

		if (c <= ' ' && tstart)
			continue;						// ignore white space preceeding token

		if (c && escaped)
		{
			#if 0 // test code treated quoted \'s differently than unquoted \'s
			if (c == '"' || c == 'n' || (c == '\\' && !quoted))
				token += '\\';
			else if (c == '\\')
				token += "\\\\\\";
			else if (quoted)
				token += "\\\\";
			#endif

			if (c == '"' || c == 'n' || c == '\\')
				token += '\\';
			token += c;						// add escaped char to token
			escaped = false;
			tstart = false;

			continue;
		}

		#if 0 // test code treated quoted \'s differently than unquoted \'s
		if (escaped)
			token += "\\\\";
		#endif

		if (c == '\\')
		{
			escaped = true;					// next char is escaped
			continue;
		}

		if (c == '"' && tstart)
		{
			quoted = true;
			tstart = false;
			continue;
		}

		if (c == 0)
			tend = true;

		if (c == '"' && quoted && (i + 1 >= input.length() || is_term_char(input[i+1])))
			tend = true;

		if (!quoted && is_term_char(c))
			tend = true;

		if (!tend)
		{
			if (c == '\\' || c == '"')
				token += '\\';
			token += c;
			tstart = false;
			continue;
		}

		//cerr << "input add ntokens " << ntokens << " needs_comma " << needs_comma << " tstart " << tstart << " tend " << tend << " quoted " << quoted << " escaped " << escaped << " pos " << i << " char " << (unsigned)c << " = '" << c << "' token '" << token << "' json " << json << endl;

		if (!ntokens++)
		{
			json = "{\"method\":\"";
			json += token;
			json += "\",\"params\":[";
		}
		else if (!tstart)
		{
			if (ntokens > 2 && needs_comma && c != ',')
				json += ",";

			auto quote = add_quotes(token);

			if (quote)
				json += "\"";

			json += token;

			if (quote)
				json += "\"";
		}

		if (c && c != ' ' && c != '"')
			json += c;						// add terminator

		needs_comma = (c != ',' && c != '{' && c != '[' && c != ':');

		token.clear();
		tstart = true;
		tend = false;
		quoted = false;
	}

	if (json.length())
		json += "]}";

	return json;
}

void do_interactive(DbConn *dbconn, TxQuery& txquery)
{
	while (true)
	{
		if (g_disable_malloc_logging)
		{
			g_disable_malloc_logging = false;

			cc_malloc_logging(false);
		}

		static string input;
		input.clear();

		while (!input.length())
		{
			{
				lock_guard<mutex> lock(g_cerr_lock);
				check_cerr_newline();
				cerr << CCEXENAME "> ";
			}

			if (!cin.good() || g_shutdown) return;

			getline(cin, input);

			if (!cin.good() || g_shutdown) return;

			//Beep(1200, 500);

			unsigned p = 0;
			for (unsigned i = 0; i < input.length(); ++i)
			{
				unsigned c = input[i];
				//cerr << "scan char 0x" << hex << c << dec << endl;
				if (c >= ' ')					// strip out (ignore) control chars
					input[p++] = c;
			}

			input.resize(p);
		}

		{
			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();
			cerr << endl;
		}

		//cerr << "input: " << input << endl;
		//cerr << "binary: " << buf2hex(input.c_str(), input.length() + 1) << endl;

		if (input == "stop" || input == "STOP")
		{
			start_shutdown();

			return;
		}

		if (input == "?" || input == "help" || input == "HELP")
		{
			input = "help";

			lock_guard<mutex> lock(g_cerr_lock);
			check_cerr_newline();

			cerr <<
			"command [params ...]\n"
			"\n"
			"- Spaces or commas separate parameters, except when escaped or inside double quotes.\n"
			"- {} and [] delimit objects and lists, except when escaped or inside double quotes.\n"
			"- The \\ is an escape character and is not included in the parameter value except for \\n.\n"
			"- A double quote only closes a quoted string when followed by a space or delimiter.\n"
			"\n"
			//"==Local commands==\n"
			//"stop - Shutdown RPC server and exit\n"
			"\n-----------------------------------------------"
			;
			cerr << endl;
		}

		auto json = input_to_json(input);

		//cerr << "json: " << json << endl;

		if (json.length())
			interactive_do_json_command(json, dbconn, txquery);
	}
}

class InteractiveJsonWriter : public Json::StyledStreamWriter
{
	string json;

	void writeValue(const Json::Value& value) override
	{
		//cerr << "InteractiveJsonWriter writeValue" << endl;

		if (value.type() != Json::realValue)
			StyledStreamWriter::writeValue(value);
		else
		{
			//cerr << "json " << json << endl;
			//cerr << "OffsetStart " << value.getOffsetStart() << " OffsetLimit " << value.getOffsetLimit() << endl;

			auto s = json.substr(value.getOffsetStart(), value.getOffsetLimit() - value.getOffsetStart());
			//cerr << s << endl;

			pushValue(s);
		}
	}

public:
	InteractiveJsonWriter(const string& s)
	 :	StyledStreamWriter("  "),
		json(s)
	{ }
};

int interactive_do_json_command(const string& json, DbConn *dbconn, TxQuery& txquery)
{
	if (!json.size())
		return 0;

	{
		lock_guard<mutex> lock(g_cerr_lock);
		check_cerr_newline();
		cerr << "Executing JSON command:\n" << json << "\n" << endl;
	}

	string response;
	int result;

	{
		ostringstream rstream;

		result = do_json_rpc(json, dbconn, txquery, rstream);

		response = rstream.str();
	}

	Json::Reader reader;
	Json::Value root, value;
	string key;

	reader.parse(response, root);

	InteractiveJsonWriter writer(response);

	lock_guard<mutex> lock(g_cerr_lock);
	check_cerr_newline();

	if (g_params.developer_mode)
	{
		cerr << "Command result:\n" << response << "\n" << endl;
	}
	else if (!reader.good())
	{
		cerr << "Command result: " << response << endl;
		cerr << "\nError parsing command result: " << reader.getFormattedErrorMessages() << endl;
		result = -1;
	}
	else
	{
		bool has_result = false;

		key = "result";
		if (root.removeMember(key, &value) && !value.isNull())
		{
			has_result = true;

			if (!value.isString())
			{
				cerr << "Result:" << endl;
				writer.write(cerr, value);
			}
			else if (value.asString().length())
			{
				cerr << "Result:" << endl;
				cerr << value.asString() << endl;
			}
		}

		key = "error";
		if (root.removeMember(key, &value) && !value.isNull())
		{
			has_result = true;

			cerr << "Error:" << endl;
			writer.write(cerr, value);

			key = "code";
			try
			{
				result = value[key].asInt();
			}
			catch (...)
			{
				result = -1;
			}
		}

		key = "id";
		root.removeMember(key, &value);

		if (!root.empty())
		{
			cerr << "Unrecognized values in response:" << endl;
			writer.write(cerr, root);
		}
		else if (!has_result)
		{
			cerr << "Unrecognized command result:" << response << endl;
			result = -1;
		}

		if (g_params.interactive)
			cerr << endl;
	}

	return result;
}