#include <iostream>
#include <array>
#include <vector>
#include <algorithm>
#include <functional>
#include <locale.h>
#include <ncurses.h>
#include <atasmart.h>
#include <sys/signal.h>
#include <dirent.h>

std::string const TITLE = "CrazyDiskInfo";
std::string const VERSION = "1.0.2";

constexpr int const STATUS_WIDTH = 80;

constexpr int const DEVICE_BAR_HEIGHT = 4;

constexpr int const VERSION_HEIGHT = 1;

constexpr int const HEALTH_COLOR = 1;
constexpr int const HEALTH_INV_COLOR = 4;
constexpr int const ATTRIBUTE_LEGEND_COLOR = 7;
constexpr int const TITLE_COLOR = 8;

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
	uint8_t threshold;
	uint64_t raw;
};

class SMART
{
public:
	std::string deviceName;
	std::string model;
	std::string firmware;
	std::string serial;
	//TODO
	//use std::optional
	std::pair<bool, uint64_t> size;
	std::pair<bool, uint64_t> powerOnCount;
	std::pair<bool, uint64_t> powerOnHour;
	std::pair<bool, double> temperature;

	std::vector<Attribute> attribute;

	SMART(std::string deviceName)
	:
	deviceName(deviceName)
	{
		SkDisk * skdisk;
		sk_disk_open(deviceName.c_str(), &skdisk);
		sk_disk_smart_read_data(skdisk);

		const SkIdentifyParsedData * data;
		sk_disk_identify_parse(skdisk, &data);
		model = data->model;
		firmware = data->firmware;
		serial = data->serial;

		uint64_t value;
		if (!sk_disk_get_size(skdisk, &value))
		{
			std::get<0>(size) = true;
			std::get<1>(size) = value;
		}
		else
		{
			std::get<0>(size) = false;
		}

		if (!sk_disk_smart_get_power_cycle(skdisk, &value))
		{
			std::get<0>(powerOnCount) = true;
			std::get<1>(powerOnCount) = value;
		}
		else
		{
			std::get<0>(powerOnCount) = false;
		}

		if (!sk_disk_smart_get_power_on(skdisk, &value))
		{
			std::get<0>(powerOnHour) = true;
			std::get<1>(powerOnHour) = value / (1000llu * 60llu * 60llu);
		}
		else
		{
			std::get<0>(powerOnHour) = false;
		}

		if (!sk_disk_smart_get_temperature(skdisk, &value))
		{
			std::get<0>(temperature) = true;
			std::get<1>(temperature) = (double)(value - 273150llu) / 1000.0;
		}
		else
		{
			std::get<0>(temperature) = false;
		}

		sk_disk_smart_parse_attributes(skdisk, [](SkDisk * skdisk, SkSmartAttributeParsedData const * data, void * userdata)
		{
			auto attribute = reinterpret_cast<std::vector<Attribute> *>(userdata);
			Attribute attr = {};
			attr.id = data->id;
			attr.name = data->name;
			attr.current = data->current_value;
			attr.worst = data->worst_value;
			attr.threshold = data->threshold;
			for (int i = 0; i < 6; ++i)
			{
				attr.raw += data->raw[i] << (8 * i);
			}
			attribute->push_back(attr);
		}, &attribute);

		sk_disk_free(skdisk);
	}
};

Health temperatureToHealth(double temperature)
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
	if ((attribute.threshold != 0) && (attribute.current < attribute.threshold))
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

std::string healthToString(Health health)
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

	wattrset(window, COLOR_PAIR(HEALTH_COLOR));
	mvwhline(window, 0, 0, '-', width);
	wattroff(window, COLOR_PAIR(HEALTH_COLOR));

	auto title = " " + TITLE + "-" + VERSION + " ";

	wattrset(window, COLOR_PAIR(TITLE_COLOR));
	mvwprintw(window, 0, (width - title.length()) / 2, title.c_str());
	wattroff(window, COLOR_PAIR(TITLE_COLOR));

	wnoutrefresh(window);
}

void drawDeviceBar(WINDOW * window, std::vector<SMART> const & smartList, int select)
{
	int x = 0;
	for (int i = 0; i < static_cast<int>(smartList.size()); ++i)
	{
		wattrset(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(smartToHealth(smartList[i]))));
		mvwprintw(window, 0, x, "%-7s", healthToString(smartToHealth(smartList[i])).c_str());
		wattroff(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(smartToHealth(smartList[i]))));

		if (std::get<0>(smartList[i].temperature))
		{
			wattrset(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(temperatureToHealth(std::get<1>(smartList[i].temperature)))));
			mvwprintw(window, 1, x, "%.1f ", smartList[i].temperature);
			waddch(window, ACS_DEGREE);
			waddstr(window, "C");
			wattroff(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(temperatureToHealth(std::get<1>(smartList[i].temperature)))));
		}
		else
		{
			mvwprintw(window, 1, x, "-- ");
			waddch(window, ACS_DEGREE);
			waddstr(window, "C");
		}

		if (i == select)
		{
			wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
			mvwprintw(window, 2, x, smartList[i].deviceName.c_str());
			wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);

			wattrset(window, COLOR_PAIR(HEALTH_COLOR));
			mvwhline(window, 3, x, '-', smartList[i].deviceName.length());
			wattroff(window, COLOR_PAIR(HEALTH_COLOR));
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
	if (std::get<0>(smart.size))
	{
		std::vector<std::string> unit = {{"Byte", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"}};
		int u = 0;
		double size = std::get<1>(smart.size);
		while (true)
		{
			double old = size;
			size /= 1024;
			if (size < 1.0)
			{
				size = old;
				break;
			}
			++u;
		}
		char s[STATUS_WIDTH];
		int len = snprintf(s, STATUS_WIDTH, " %s [%.1f %s] ", smart.model.c_str(), size, unit[u].c_str());
		wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		mvwprintw(window, 0, (STATUS_WIDTH - len) / 2, "%s", s);
		wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
	}
	else
	{
		char s[STATUS_WIDTH];
		int len = snprintf(s, STATUS_WIDTH, " %s [--] ", smart.model.c_str());
		wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		mvwprintw(window, 0, (STATUS_WIDTH - len) / 2, "%s", s);
		wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
	}

	wattrset(window, COLOR_PAIR(HEALTH_COLOR));
	mvwprintw(window, 2, (int)(STATUS_WIDTH * (1.0 / 5)), "Firmware:");
	wattroff(window, COLOR_PAIR(HEALTH_COLOR));
	wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
	wprintw(window, " %s", smart.firmware.c_str());
	wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);

	wattrset(window, COLOR_PAIR(HEALTH_COLOR));
	mvwprintw(window, 3, (int)(STATUS_WIDTH * (1.0 / 5)), "Serial:  ");
	wattroff(window, COLOR_PAIR(HEALTH_COLOR));
	wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
	if (option.hideSerial)
	{
		wprintw(window, " ********************");
	}
	else
	{
		wprintw(window, " %s", smart.serial.c_str());
	}
	wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);

	wattrset(window, COLOR_PAIR(HEALTH_COLOR));
	mvwprintw(window, 1, 1, "Status");
	wattroff(window, COLOR_PAIR(HEALTH_COLOR));
	wattrset(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(smartToHealth(smart))));
	mvwprintw(window, 2, 2, "+--------+");
	mvwprintw(window, 3, 2, "|        |");
	mvwprintw(window, 4, 2, "+--------+");
	mvwprintw(window, 3, 2 + ((sizeof("|        |") - healthToString(smartToHealth(smart)).length()) / 2), "%s", healthToString(smartToHealth(smart)).c_str());
	wattroff(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(smartToHealth(smart))));

	if (std::get<0>(smart.temperature))
	{
		wattrset(window, COLOR_PAIR(HEALTH_COLOR));
		mvwprintw(window, 5, 1, "Temperature");
		wattroff(window, COLOR_PAIR(HEALTH_COLOR));
		wattrset(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(temperatureToHealth(std::get<1>(smart.temperature)))));
		mvwprintw(window, 6, 2, "  %0.1f ", std::get<1>(smart.temperature));
		waddch(window, ACS_DEGREE);
		waddstr(window, "C  ");
		wattroff(window, COLOR_PAIR(HEALTH_INV_COLOR + static_cast<int>(temperatureToHealth(std::get<1>(smart.temperature)))));
	}
	else
	{
		mvwprintw(window, 5, 1, "Temperature");
		mvwprintw(window, 6, 2, "  -- ");
		waddch(window, ACS_DEGREE);
		waddstr(window, "C  ");
	}

	if (std::get<0>(smart.powerOnCount))
	{
		wattrset(window, COLOR_PAIR(HEALTH_COLOR));
		mvwprintw(window, 2, (int)(STATUS_WIDTH * (3.0 / 5)), "Power On Count:");
		wattroff(window, COLOR_PAIR(HEALTH_COLOR));
		wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		wprintw(window, " %llu ", std::get<1>(smart.powerOnCount));
		wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		wattrset(window, COLOR_PAIR(HEALTH_COLOR));
		wprintw(window, "count");
	}
	else
	{
		mvwprintw(window, 2, (int)(STATUS_WIDTH * (3.0 / 5)), "Power On Count:");
		wprintw(window, " -- count");
	}

	if (std::get<0>(smart.powerOnHour))
	{
		wattrset(window, COLOR_PAIR(HEALTH_COLOR));
		mvwprintw(window, 3, (int)(STATUS_WIDTH * (3.0 / 5)), "Power On Hours:");
		wattroff(window, COLOR_PAIR(HEALTH_COLOR));
		wattrset(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		wprintw(window, " %llu ", std::get<1>(smart.powerOnHour));
		wattroff(window, COLOR_PAIR(HEALTH_COLOR) | A_BOLD);
		wattrset(window, COLOR_PAIR(HEALTH_COLOR));
		wprintw(window, "hours");
		wattroff(window, COLOR_PAIR(HEALTH_COLOR));
	}
	else
	{
		mvwprintw(window, 3, (int)(STATUS_WIDTH * (3.0 / 5)), "Power On Hours:");
		wprintw(window, " -- hours");
	}

	wattrset(window, COLOR_PAIR(ATTRIBUTE_LEGEND_COLOR));
	mvwprintw(window, 8, 1, " Status  ID AttributeName                Current Worst Threshold   Raw Values ");
	wattroff(window, COLOR_PAIR(ATTRIBUTE_LEGEND_COLOR));
	for (int i = 0; i < static_cast<int>(smart.attribute.size()); ++i)
	{
		wattrset(window, COLOR_PAIR(HEALTH_COLOR + static_cast<int>(attributeToHealth(smart.attribute[i]))));
#ifndef RAWDEC
		mvwprintw(window, 9 + i, 1, " %-7s %02X %-28s %7d %5d %9d %012X ",
#else
		mvwprintw(window, 9 + i, 1, " %-7s %02X %-28s %7d %5d %9d %012d ",
#endif//RAWDEC
			healthToString(attributeToHealth(smart.attribute[i])).c_str(),
			smart.attribute[i].id,
			smart.attribute[i].name.c_str(),
			smart.attribute[i].current,
			smart.attribute[i].worst,
			smart.attribute[i].threshold,
			smart.attribute[i].raw);
		wattroff(window, COLOR_PAIR(HEALTH_COLOR + static_cast<int>(attributeToHealth(smart.attribute[i]))));
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
	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	curs_set(0);
	getmaxyx(stdscr, height, width);

	start_color();
	init_pair(HEALTH_COLOR, COLOR_CYAN, COLOR_BLACK);
	init_pair(HEALTH_COLOR + 1, COLOR_BLACK, COLOR_YELLOW);
	init_pair(HEALTH_COLOR + 2, COLOR_WHITE, COLOR_RED);
	init_pair(HEALTH_INV_COLOR, COLOR_BLACK, COLOR_CYAN);
	init_pair(HEALTH_INV_COLOR + 1, COLOR_BLACK, COLOR_YELLOW);
	init_pair(HEALTH_INV_COLOR + 2, COLOR_WHITE, COLOR_RED);
	init_pair(ATTRIBUTE_LEGEND_COLOR, COLOR_BLACK, COLOR_GREEN);
	init_pair(TITLE_COLOR, COLOR_YELLOW, COLOR_BLACK);

	std::vector<SMART> smartList;
	auto dir = opendir("/sys/block");
	while (auto e = readdir(dir))
	{
		if (std::string(".") != std::string(e->d_name) &&
			std::string("..") != std::string(e->d_name) &&
			std::string("ram") != std::string(e->d_name).substr(0,3) &&
			std::string("loop") != std::string(e->d_name).substr(0,4))
		{
			SkDisk * skdisk;
			SkBool b;
			int f = sk_disk_open((std::string("/dev/") + std::string(e->d_name)).c_str(), &skdisk);
			if (f < 0)
			{
				continue;
			}
			int smart_ret = sk_disk_smart_is_available(skdisk, &b);
			sk_disk_free(skdisk);
			if (smart_ret < 0)
			{
				continue;
			}
			if (b)
			{
				smartList.push_back(SMART(std::string("/dev/") + std::string(e->d_name)));
			}
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
