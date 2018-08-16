#include <iostream>
#include <array>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <memory>
#include <string_view>
#include <optional>
#include <tuple>
#include <locale.h>
#include <ncurses.h>
#include <sys/signal.h>
#include <nlohmann/json.hpp>

std::string const TITLE = "CrazyDiskInfo";
std::string const VERSION = "1.0.2";

constexpr int const STATUS_WIDTH = 80;

constexpr int const DEVICE_BAR_HEIGHT = 4;

constexpr int const VERSION_HEIGHT = 1;

int width;
int height;

class Option
{
public:
	bool hideSerial;
	Option()
	:
	hideSerial(false)
	{
	}
};

enum class Health
{
	Good,
	Caution,
	Bad
};

class Attribute
{
public:
	uint8_t id;
	std::string name;
	uint8_t current;
	uint8_t worst;
	std::optional<uint8_t> threshold;
	uint64_t raw;
};

class SMART
{
public:
	std::string deviceName;
	std::string model;
	std::string firmware;
	std::string serial;
	std::optional<uint64_t> size;
	std::optional<double> temperature;
	std::string standard;
	int32_t rpm;

	std::vector<Attribute> attribute;

	std::optional<uint64_t> powerOnCount() const
	{
		if (auto e = std::find_if(attribute.cbegin(), attribute.cend(), [](auto const & a){return a.id == 0x0C;}); e != attribute.end())
		{
			return std::make_optional(e->raw);
		}
		else
		{
			return std::nullopt;
		}
	}

	std::optional<uint64_t> powerOnHour() const
	{
		if (auto e = std::find_if(attribute.cbegin(), attribute.cend(), [](auto const & a){return a.id == 0x09;}); e != attribute.end())
		{
			return std::make_optional(e->raw);
		}
		else
		{
			return std::nullopt;
		}
	}
};

std::optional<std::string> exec(std::string_view const & cmd)
{
	auto raw = std::unique_ptr<FILE, decltype(&pclose)>(popen(cmd.data(), "r"), pclose);

	if (raw)
	{
		std::array<char, 256> buffer;
		std::string result;

		while (!feof(raw.get()))
		{
			if (fgets(buffer.data(), 256, raw.get()) != nullptr)
			{
				result += buffer.data();
			}
		}

		return result;
	}
	else
	{
		return std::nullopt;
	}
}

std::optional<SMART> deviceNameToSMART(std::string_view const & deviceName)
{
	try
	{
		auto const t = exec(std::string("smartctl -ja ") + std::string(deviceName));
		if (!t.has_value())
		{
			return std::nullopt;
		}
		auto const j = nlohmann::json::parse(*t);
		if (j["smartctl"]["exit_status"].get<int>())
		{
			return std::nullopt;
		}

		auto smart = SMART();
		smart.firmware = j["firmware_version"].get<std::string>();
		smart.serial = j["serial_number"].get<std::string>();
		smart.model = j["model_name"].get<std::string>();
		smart.deviceName = j["device"]["name"].get<std::string>();
		smart.standard = j["ata_version"]["string"].get<std::string>();
		smart.rpm = j["rotation_rate"].get<int32_t>();

		if (j["temperature"]["current"].is_number())
		{
			smart.temperature = std::make_optional(j["temperature"]["current"].get<double>());
		}

		if (j["user_capacity"]["bytes"]["n"].is_number())
		{
			smart.size = std::make_optional(j["user_capacity"]["bytes"]["n"].get<uint64_t>());
		}

		std::vector<Attribute> attribute;
		for (auto e : j["ata_smart_attributes"]["table"])
		{
			auto attr = Attribute();
			attr.id = e["id"].get<uint8_t>();
			attr.name = e["name"].get<std::string>();
			attr.current = e["value"].get<uint8_t>();
			attr.worst = e["worst"].get<uint8_t>();
			if (e["thresh"].is_number())
			{
				attr.threshold = std::make_optional(e["thresh"].get<uint8_t>());
			}
			attr.raw = e["raw"]["value"].get<uint64_t>();
			attribute.push_back(attr);
		}
		smart.attribute = attribute;

		return std::make_optional(smart);
	}
	catch(...)
	{
	}

	return std::nullopt;
}

Health temperatureToHealth(double const temperature)
{
	if (temperature < 50)
	{
		return Health::Good;
	}
	else if (temperature < 55)
	{
		return Health::Caution;
	}
	else
	{
		return Health::Bad;
	}
}

Health attributeToHealth(Attribute const & attribute)
{
	if (attribute.threshold.has_value() && (attribute.current < attribute.threshold.value()))
	{
		return Health::Bad;
	}
	else if (((attribute.id == 0x05) || (attribute.id == 0xC5) || (attribute.id == 0xC6)) && (attribute.raw != 0))
	{
		return Health::Caution;
	}
	else
	{
		return Health::Good;
	}
}

Health smartToHealth(SMART const & smart)
{
	return attributeToHealth(*std::max_element(smart.attribute.cbegin(), smart.attribute.cend(), [](Attribute const & lhs, Attribute const & rhs)
	{
		return static_cast<int>(attributeToHealth(lhs)) < static_cast<int>(attributeToHealth(rhs));
	}));
}

std::string healthToString(Health const health)
{
	switch (health)
	{
		case Health::Good:
			return "Good";
		case Health::Caution:
			return "Caution";
		case Health::Bad:
			return "Bad";
		default:
			return "Unknown";
	}
}

void drawVersion(WINDOW * window)
{
	wresize(window, VERSION_HEIGHT, width);

	wattrset(window, COLOR_PAIR(4));
	mvwhline(window, 0, 0, '-', width);
	wattroff(window, COLOR_PAIR(4));

	auto const title = " " + TITLE + "-" + VERSION + " ";

	wattrset(window, COLOR_PAIR(8));
	mvwprintw(window, 0, (width - title.length()) / 2, title.c_str());
	wattroff(window, COLOR_PAIR(8));

	wnoutrefresh(window);
}

void drawDeviceBar(WINDOW * window, std::vector<SMART> const & smartList, int select)
{
	int x = 0;
	for (int i = 0; i < static_cast<int>(smartList.size()); ++i)
	{
		wattrset(window, COLOR_PAIR(1 + static_cast<int>(smartToHealth(smartList[i]))));
		mvwprintw(window, 0, x, "%-7s", healthToString(smartToHealth(smartList[i])).c_str());
		wattroff(window, COLOR_PAIR(1 + static_cast<int>(smartToHealth(smartList[i]))));

		if (auto const t = smartList[i].temperature)
		{
			wattrset(window, COLOR_PAIR(1 + static_cast<int>(temperatureToHealth(*t))));
			mvwprintw(window, 1, x, "%.1f ", *t);
			waddch(window, ACS_DEGREE);
			waddstr(window, "C");
			wattroff(window, COLOR_PAIR(1 + static_cast<int>(temperatureToHealth(*t))));
		}
		else
		{
			mvwprintw(window, 1, x, "-- ");
			waddch(window, ACS_DEGREE);
			waddstr(window, "C");
		}

		if (i == select)
		{
			wattrset(window, COLOR_PAIR(4) | A_BOLD);
			mvwprintw(window, 2, x, smartList[i].deviceName.c_str());
			wattroff(window, COLOR_PAIR(4) | A_BOLD);

			wattrset(window, COLOR_PAIR(4));
			mvwhline(window, 3, x, '-', smartList[i].deviceName.length());
			wattroff(window, COLOR_PAIR(4));
		}
		else
		{
			mvwprintw(window, 2, x, smartList[i].deviceName.c_str());
			mvwhline(window, 3, x, ' ', smartList[i].deviceName.length());
		}
		x += smartList[i].deviceName.length() + 1;
	}
	pnoutrefresh(window, 0, 0, 1, 0, DEVICE_BAR_HEIGHT, width - 1);
}

void drawStatus(WINDOW * window, SMART const & smart, Option const & option)
{
	wresize(window, 10 + smart.attribute.size(), STATUS_WIDTH);
	wborder(window, '|', '|', '-', '-', '+', '+', '+', '+');
	if (smart.size.has_value())
	{
		std::vector<std::string> const unit = {{"Byte", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"}};
		int u = 0;
		double size = smart.size.value();
		while ((size / 1024.0) > 1.0)
		{
			size /= 1024.0;
			++u;
		}
		char s[STATUS_WIDTH];
		int len = std::snprintf(s, STATUS_WIDTH, " %s [%.1f %s] ", smart.model.c_str(), size, unit[u].c_str());
		wattrset(window, COLOR_PAIR(4) | A_BOLD);
		mvwprintw(window, 0, (STATUS_WIDTH - len) / 2, "%s", s);
		wattroff(window, COLOR_PAIR(4) | A_BOLD);
	}
	else
	{
		char s[STATUS_WIDTH];
		int len = std::snprintf(s, STATUS_WIDTH, " %s [--] ", smart.model.c_str());
		wattrset(window, COLOR_PAIR(4) | A_BOLD);
		mvwprintw(window, 0, (STATUS_WIDTH - len) / 2, "%s", s);
		wattroff(window, COLOR_PAIR(4) | A_BOLD);
	}

	wattrset(window, COLOR_PAIR(4));
	mvwprintw(window, 2, static_cast<int>(STATUS_WIDTH * (1.0 / 5)), "Firmware:");
	wattroff(window, COLOR_PAIR(4));
	wattrset(window, COLOR_PAIR(4) | A_BOLD);
	wprintw(window, " %s", smart.firmware.c_str());
	wattroff(window, COLOR_PAIR(4) | A_BOLD);

	wattrset(window, COLOR_PAIR(4));
	mvwprintw(window, 3, static_cast<int>(STATUS_WIDTH * (1.0 / 5)), "Serial:  ");
	wattroff(window, COLOR_PAIR(4));
	wattrset(window, COLOR_PAIR(4) | A_BOLD);
	if (option.hideSerial)
	{
		wprintw(window, " ********************");
	}
	else
	{
		wprintw(window, " %s", smart.serial.c_str());
	}
	wattroff(window, COLOR_PAIR(4) | A_BOLD);

	wattrset(window, COLOR_PAIR(4));
	mvwprintw(window, 1, 1, "Status");
	wattroff(window, COLOR_PAIR(4));
	wattrset(window, COLOR_PAIR(1 + static_cast<int>(smartToHealth(smart))));
	mvwprintw(window, 2, 2, "+--------+");
	mvwprintw(window, 3, 2, "|        |");
	mvwprintw(window, 4, 2, "+--------+");
	mvwprintw(window, 3, 2 + ((sizeof("|        |") - healthToString(smartToHealth(smart)).length()) / 2), "%s", healthToString(smartToHealth(smart)).c_str());
	wattroff(window, COLOR_PAIR(1 + static_cast<int>(smartToHealth(smart))));

	if (auto const t = smart.temperature)
	{
		wattrset(window, COLOR_PAIR(4));
		mvwprintw(window, 5, 1, "Temperature");
		wattroff(window, COLOR_PAIR(4));
		wattrset(window, COLOR_PAIR(1 + static_cast<int>(temperatureToHealth(*t))));
		mvwprintw(window, 6, 2, "  %.1f ", *t);
		waddch(window, ACS_DEGREE);
		waddstr(window, "C  ");
		wattroff(window, COLOR_PAIR(1 + static_cast<int>(temperatureToHealth(*t))));
	}
	else
	{
		mvwprintw(window, 5, 1, "Temperature");
		mvwprintw(window, 6, 2, "  -- ");
		waddch(window, ACS_DEGREE);
		waddstr(window, "C  ");
	}

	if (auto const count = smart.powerOnCount())
	{
		wattrset(window, COLOR_PAIR(4));
		mvwprintw(window, 2, static_cast<int>(STATUS_WIDTH * (3.0 / 5)), "Power On Count:");
		wattroff(window, COLOR_PAIR(4));
		wattrset(window, COLOR_PAIR(4) | A_BOLD);
		wprintw(window, " %llu ", *count);
		wattroff(window, COLOR_PAIR(4) | A_BOLD);
		wattrset(window, COLOR_PAIR(4));
		wprintw(window, "count");
	}
	else
	{
		mvwprintw(window, 2, static_cast<int>(STATUS_WIDTH * (3.0 / 5)), "Power On Count:");
		wprintw(window, " -- count");
	}

	if (auto const hour = smart.powerOnHour())
	{
		wattrset(window, COLOR_PAIR(4));
		mvwprintw(window, 3, static_cast<int>(STATUS_WIDTH * (3.0 / 5)), "Power On Hours:");
		wattroff(window, COLOR_PAIR(4));
		wattrset(window, COLOR_PAIR(4) | A_BOLD);
		wprintw(window, " %llu ", *hour);
		wattroff(window, COLOR_PAIR(4) | A_BOLD);
		wattrset(window, COLOR_PAIR(4));
		wprintw(window, "hours");
		wattroff(window, COLOR_PAIR(4));
	}
	else
	{
		mvwprintw(window, 3, static_cast<int>(STATUS_WIDTH * (3.0 / 5)), "Power On Hours:");
		wprintw(window, " -- hours");
	}

	wattrset(window, COLOR_PAIR(7));
	mvwprintw(window, 8, 1, " Status  ID AttributeName                Current Worst Threshold   Raw Values ");
	wattroff(window, COLOR_PAIR(7));
	for (int i = 0; i < static_cast<int>(smart.attribute.size()); ++i)
	{
		std::array<char, 10> threshold = {};
		if (auto t = smart.attribute[i].threshold)
		{
			std::snprintf(threshold.data(), threshold.size(), "%9d", *t);
		}
		else
		{
			std::string("       --").copy(threshold.data(), threshold.size());
		}

		wattrset(window, COLOR_PAIR(4 + static_cast<int>(attributeToHealth(smart.attribute[i]))));
#ifndef RAWDEC
		mvwprintw(window, 9 + i, 1, " %-7s %02X %-28s %7d %5d %9s %012X ",
#else
		mvwprintw(window, 9 + i, 1, " %-7s %02X %-28s %7d %5d %9s %012d ",
#endif//RAWDEC
			healthToString(attributeToHealth(smart.attribute[i])).c_str(),
			smart.attribute[i].id,
			smart.attribute[i].name.c_str(),
			smart.attribute[i].current,
			smart.attribute[i].worst,
			threshold.data(),
			smart.attribute[i].raw);
		wattroff(window, COLOR_PAIR(4 + static_cast<int>(attributeToHealth(smart.attribute[i]))));
	}
	pnoutrefresh(window, 0, 0,
			5, std::max(0, (width - STATUS_WIDTH) / 2),
			std::min(height - 1, 5 + 10 + static_cast<int>(smart.attribute.size())), std::min(width - 1, std::max(0, (width - STATUS_WIDTH) / 2) + STATUS_WIDTH));
}

std::function<void(void)> update;
void actionWINCH(int)
{
	clear();
	endwin();
	refresh();
	update();
}

int main()
{
	auto scan = exec("smartctl -j --scan").value();
	if (!nlohmann::json::accept(scan))
	{
		std::cerr << scan << std::endl;
		return 1;
	}
	auto devices = nlohmann::json::parse(scan)["devices"];

	std::vector<SMART> smartList;
	for (auto device : devices)
	{
		if (auto const d = deviceNameToSMART(device["name"].get<std::string>()))
		{
			smartList.push_back(*d);
		}
	}
	std::sort(smartList.begin(), smartList.end(), [](SMART const & lhs, SMART const & rhs){return lhs.deviceName < rhs.deviceName;});

	if (smartList.size() == 0)
	{
		endwin();
		std::cerr << "No S.M.A.R.T readable devices." << std::endl;
		std::cerr << "If you are non-root user, please use sudo or become root." << std::endl;
		return 1;
	}

	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	getmaxyx(stdscr, height, width);

	start_color();
	init_pair(1, COLOR_BLACK, COLOR_CYAN);
	init_pair(2, COLOR_BLACK, COLOR_YELLOW);
	init_pair(3, COLOR_WHITE, COLOR_RED);
	init_pair(4, COLOR_CYAN, COLOR_BLACK);
	init_pair(5, COLOR_BLACK, COLOR_YELLOW);
	init_pair(6, COLOR_WHITE, COLOR_RED);
	init_pair(7, COLOR_BLACK, COLOR_GREEN);
	init_pair(8, COLOR_YELLOW, COLOR_BLACK);

	int select = 0;
	Option option;

	WINDOW * windowVersion;
	windowVersion = newwin(1, width, 0, 0);

	WINDOW * windowDeviceBar;
	{
	int x = 0;
	for (auto && e : smartList)
	{
		x += e.deviceName.length() + 1;
	}
	windowDeviceBar = newpad(DEVICE_BAR_HEIGHT, x);
	keypad(windowDeviceBar, true);
	}

	WINDOW * windowDeviceStatus;
	windowDeviceStatus = newpad(10 + smartList[select].attribute.size(), STATUS_WIDTH);

	update = [&]()
	{
		getmaxyx(stdscr, height, width);
		wclear(windowVersion);
		drawVersion(windowVersion);
		wclear(windowDeviceBar);
		drawDeviceBar(windowDeviceBar, smartList, select);
		wclear(windowDeviceStatus);
		drawStatus(windowDeviceStatus, smartList[select], option);
		doupdate();
	};
	update();

	{
		struct sigaction s = {{actionWINCH}};
		sigaction(SIGWINCH, &s, nullptr);
	}

	while (true)
	{
		switch (wgetch(windowDeviceBar))
		{
			case KEY_HOME:
				select = 0;
				clear();
				refresh();
				update();
				break;

			case KEY_END:
				select = static_cast<int>(smartList.size()) - 1;
				clear();
				refresh();
				update();
				break;

			case KEY_LEFT:
			case 'h':
				select = std::max(select - 1, 0);
				clear();
				refresh();
				update();
				break;

			case KEY_RIGHT:
			case 'l':
				select = std::min(select + 1, static_cast<int>(smartList.size()) - 1);
				clear();
				refresh();
				update();
				break;


			case 's':
				option.hideSerial = !option.hideSerial;
				clear();
				refresh();
				update();
				break;


			case 'q':
				endwin();
				return 0;

			default:
				break;
		}
	}
}
