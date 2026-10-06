// ---- opening a playlist ---------------------------------------------------------

static void startLoading(int index)
{
	if (index < 0 || index >= (int)g_sources.size())
		return;
	cancelLink();
	stopGenreLoading();
	{
		std::lock_guard<std::mutex> lock(g_libMutex);
		g_libRequest++;
		g_libDone = 0;
	}
	{
		std::lock_guard<std::mutex> lock(g_drillMutex);
		g_drillRequest++;
		g_drillDone = 0;
	}
	player::stop();
	gfx::videoClear();
	g_playing = -1;
	g_nowOn = false;
	g_fullscreen = false;
	g_keyboard = false;
	g_haveBrowse = false;
	g_active = index;
	g_activeId = sourceId(g_sources[(size_t)index]);
	writeTextFile(g_dataDir + "/active.txt", g_activeId);
	g_screen = LOADING;
	int request;
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		request = ++g_loadRequest;
		g_loadState = 0;
	}
	g_loadStarted = nowSeconds();
	const Source source = g_sources[(size_t)index];
	const bool useCache = g_loadUseCache;
	g_loadUseCache = true;
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		g_refreshedReady = false;
	}
	startThread("playlist", [source, request, useCache] { loadPlaylist(source, request, useCache); });
}

// Gives up waiting for a playlist and goes to Settings, so a slow or dead address never traps the viewer.
static void cancelLoading()
{
	if (g_screen != LOADING)
		return;
	{
		std::lock_guard<std::mutex> lock(g_loadMutex);
		g_loadRequest++;
		g_loadState = 0;
	}
	httpCancel();
	logLine("playlist: loading cancelled by the viewer");
	openManager("Loading was cancelled. Pick another playlist, or open this one again to retry.");
}

static void reloadPlaylist()
{
	// Reload means "ask the server": the saved category lists are put aside
	for (int tab = 0; tab < 3; tab++)
		remove(cachePath(g_activeId, "groups" + std::to_string(tab)).c_str());
	g_loadUseCache = false;
	startLoading(g_active);
}

// ---- on-screen keyboard ---------------------------------------------------------

static std::string* g_keyTarget = nullptr;
static std::string g_keyLabel;
static size_t g_keyLimit = 40;
static bool g_keyIsFilter = false, g_keyShift = false;
static int g_keyRow = 1, g_keyCol = 0;
static const int kKeyRowCount = 6;
static const char* const kKeyRows[5] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm", "./:-_@?=&%" };
static const char* const kKeySpecial[5] = { "Shift", "Space", "Delete", "Clear", "Done" };

static void openKeyboard(std::string* target, size_t limit, bool isFilter, const std::string& label)
{
	g_keyTarget = target;
	g_keyLimit = limit;
	g_keyIsFilter = isFilter;
	g_keyLabel = label;
	g_keyShift = false;
	g_keyRow = 1;
	g_keyCol = 0;
	g_keyboard = true;
}

static int keyCount(int row)
{
	return row < 5 ? (int)strlen(kKeyRows[row]) : 5;
}

static void pressKey(int row, int col)
{
	if (!g_keyTarget)
		return;
	std::string& text = *g_keyTarget;
	const int keep = g_keyIsFilter ? selectedChannel() : -1;
	if (row < 5)
	{
		char c = kKeyRows[row][col];
		if (g_keyShift && c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		if (text.size() < g_keyLimit)
			text += c;
	}
	else if (col == 0)
		g_keyShift = !g_keyShift;
	else if (col == 1)
	{
		if (!text.empty() && text.size() < g_keyLimit)
			text += ' ';
	}
	else if (col == 2)
	{
		if (!text.empty())
			text.pop_back();
	}
	else if (col == 3)
		text.clear();
	else
		g_keyboard = false;
	if (g_keyIsFilter)
	{
		applyFilter(keep);
		if (!g_keyboard && !T->filterCats)
			g_focus = F_CHANNELS;
	}
}

static void drawKeyboard()
{
	const float w = 1100, h = 580, x = (1920 - w) / 2, y = 470, key = 98, keyH = 66, gap = 6;
	gfx::rect(0, 0, 1920, 1080, 0x00000080);
	clickable(0, 0, 1920, 1080, [] {});                       // nothing behind the keyboard can be clicked
	gfx::panel(x, y, w, h, 18, 2, kBorder, kDialog);
	const float lw = gfx::text(x + 30, y + 22, 24, kMuted, g_keyLabel + ":");
	gfx::panel(x + 46 + lw, y + 12, w - 76 - lw, 46, 10, 1, kLine, kDeep);
	const std::string shown = (g_keyTarget ? *g_keyTarget : std::string()) + "_";
	// long text (web addresses) shows its end, where the typing happens
	std::string tail = shown;
	while (tail.size() > 1 && gfx::textWidth(24, tail) > w - 110 - lw)
		tail.erase(0, 1);
	gfx::text(x + 60 + lw, y + 22, 24, kText, tail);

	for (int row = 0; row < kKeyRowCount; row++)
	{
		const int count = keyCount(row);
		const float kw = row < 5 ? key : 204;
		const float rowX = x + (w - ((float)count * kw + (float)(count - 1) * gap)) / 2;
		for (int col = 0; col < count; col++)
		{
			const float kx = rowX + (float)col * (kw + gap), ky = y + 76 + (float)row * (keyH + gap);
			const bool selected = (!pointerOn() && row == g_keyRow && col == g_keyCol) || hovering(kx, ky, kw, keyH);
			const bool lit = row == 5 && col == 0 && g_keyShift;
			gfx::panel(kx, ky, kw, keyH, 10, selected ? 2.0f : 1.0f, selected ? kBorder : kLine, selected || lit ? kSelected : kTile);
			std::string label;
			if (row < 5)
			{
				char c = kKeyRows[row][col];
				if (g_keyShift && c >= 'a' && c <= 'z')
					c = (char)(c - 32);
				label = std::string(1, c);
			}
			else
				label = kKeySpecial[col];
			centredTight(kx, kw, ky + (row < 5 ? 17.0f : 20.0f), row < 5 ? 28.0f : 23.0f, selected || lit ? kAccent : kText, label);
			clickable(kx, ky, kw, keyH, [row, col] { g_keyRow = row; g_keyCol = col; pressKey(row, col); });
		}
	}
	centredIn(x, w, y + h - 34, 19, kMuted, "\xE2\x9C\x95 Type    \xE2\x96\xA1 Delete    L1 Shift    \xE2\x97\x8B / \xE2\x96\xB3 Done    Left stick  Pointer");
}

static void keyboardInput(Action action)
{
	switch (action)
	{
	case A_UP:
	case A_DOWN:
	{
		// keep roughly the same place across rows of different lengths
		const float place = ((float)g_keyCol + 0.5f) / (float)keyCount(g_keyRow);
		g_keyRow = (g_keyRow + (action == A_UP ? kKeyRowCount - 1 : 1)) % kKeyRowCount;
		g_keyCol = (int)(place * (float)keyCount(g_keyRow));
		break;
	}
	case A_LEFT: g_keyCol = (g_keyCol + keyCount(g_keyRow) - 1) % keyCount(g_keyRow); break;
	case A_RIGHT: g_keyCol = (g_keyCol + 1) % keyCount(g_keyRow); break;
	case A_OK: pressKey(g_keyRow, g_keyCol); break;
	case A_FAVOURITE: pressKey(5, 2); break;                  // square: delete
	case A_PAGE_UP: pressKey(5, 0); break;                    // L1: shift
	case A_BACK:
	case A_SEARCH:
	case A_MENU: pressKey(5, 4); break;                       // done
	default: break;
	}
}

// ---- "add playlist" form --------------------------------------------------------

struct FormField { std::string label, value, hint; };
static bool g_form = false;
static std::string g_formKind, g_formTitle, g_formError;
static std::vector<FormField> g_formFields;
static int g_formSel = 0;                       // the fields, then Save, then Cancel

static int g_formEdit = -1;                     // the playlist being edited, or -1 when adding

static void openForm(const std::string& kind, int editIndex)
{
	const Source* editing = editIndex >= 0 ? &g_sources[(size_t)editIndex] : nullptr;
	g_formKind = kind;
	g_formEdit = editIndex;
	g_formError.clear();
	g_formFields.clear();
	g_formFields.push_back({ "Name", editing ? editing->name : "", "optional" });
	if (kind == "url")
	{
		g_formTitle = editing ? "Edit M3U playlist" : "Add M3U playlist from a web address";
		g_formFields.push_back({ "Playlist URL", editing ? editing->a : "http://", "the address of the .m3u or .m3u8 file" });
		g_formFields.push_back({ "User agent", editing ? editing->ua : "", "optional; leave empty unless your provider needs one" });
	}
	else if (kind == "xtream")
	{
		g_formTitle = editing ? "Edit Xtream Codes account" : "Add Xtream Codes account";
		g_formFields.push_back({ "Server URL", editing ? editing->a : "http://", "for example http://example.com:8080" });
		g_formFields.push_back({ "Username", editing ? editing->b : "", "" });
		g_formFields.push_back({ "Password", editing ? editing->c : "", "" });
		g_formFields.push_back({ "MAC address", editing ? editing->mac : "", "optional; if your provider ties the account to one, like 00:1A:79:12:34:56" });
		g_formFields.push_back({ "User agent", editing ? editing->ua : "", "optional" });
	}
	else
	{
		g_formTitle = editing ? "Edit Stalker portal" : "Add Stalker / Ministra portal";
		g_formFields.push_back({ "Portal URL", editing ? editing->a : "http://", "for example http://example.com/c/" });
		g_formFields.push_back({ "MAC address", editing ? editing->b : "00:1A:79:", "the one registered with your provider" });
	}
	g_formSel = 1;
	g_form = true;
}

static std::string hostOf(const std::string& address)
{
	const size_t start = address.find("://");
	const size_t from = start == std::string::npos ? 0 : start + 3;
	const size_t end = address.find_first_of("/:?", from);
	return address.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

static void submitForm()
{
	Source source;
	const std::string name = trim(g_formFields[0].value);
	const std::string address = trim(g_formFields[1].value);
	if (address.find("://") == std::string::npos || hostOf(address).empty())
	{
		g_formError = "Enter the web address, starting with http:// or https://";
		return;
	}
	if (g_formKind == "url")
	{
		source = Source{ "url", name.empty() ? hostOf(address) : name, address, "", "", "", trim(g_formFields[2].value) };
	}
	else if (g_formKind == "xtream")
	{
		const std::string user = trim(g_formFields[2].value), pass = trim(g_formFields[3].value);
		const std::string typedMac = trim(g_formFields[4].value), mac = normalizeMac(typedMac);
		if (!typedMac.empty() && mac.empty())
		{
			g_formError = "The MAC address needs six pairs of digits, like 00:1A:79:12:34:56 (or leave it empty)";
			return;
		}
		if ((user.empty() || pass.empty()) && mac.empty())
		{
			g_formError = "Enter the username and password (or, for a MAC-only account, just the MAC address)";
			return;
		}
		source = Source{ "xtream", name.empty() ? hostOf(address) : name, address, user, pass, "", trim(g_formFields[5].value), mac };
	}
	else
	{
		const std::string mac = normalizeMac(g_formFields[2].value);
		if (mac.empty())
		{
			g_formError = "The MAC address needs six pairs of digits, like 00:1A:79:12:34:56";
			return;
		}
		source = Source{ "stalker", name.empty() ? hostOf(address) : name, address, mac, "", "", "" };
	}

	int index = g_formEdit;
	if (index >= 0)
	{
		// Editing keeps the playlist's identity (and so its favourites); its saved copy is out of date.
		source.id = sourceId(g_sources[(size_t)index]);
		remove((g_dataDir + "/cache-" + source.id + ".m3u").c_str());
		g_sources[(size_t)index] = source;
	}
	else
	{
		source.id = sourceId(source);
		for (int i = 0; i < (int)g_sources.size(); i++)
			if (sourceId(g_sources[(size_t)i]) == source.id)
				index = i;
		if (index < 0)
		{
			g_sources.push_back(source);
			index = (int)g_sources.size() - 1;
		}
		else
			g_sources[(size_t)index] = source;
	}
	saveSources(g_dataDir, g_sources);
	g_form = false;
	startLoading(index);
}

static void editField(int index)
{
	g_formSel = index;
	g_formError.clear();
	openKeyboard(&g_formFields[(size_t)index].value, 300, false, g_formFields[(size_t)index].label);
}

static void drawForm()
{
	const int fields = (int)g_formFields.size();
	const float rowH = fields > 5 ? 96.0f : 104.0f;
	const float w = 1100, h = 150 + (float)fields * rowH + 130, x = (1920 - w) / 2, y = (1080 - h) / 2;
	gfx::rect(0, 0, 1920, 1080, 0x00000090);
	clickable(0, 0, 1920, 1080, [] {});
	gfx::panel(x, y, w, h, 20, 2, kLine, kDialog);
	gfx::text(x + 40, y + 32, 32, kText, g_formTitle, w - 80);

	for (int i = 0; i < fields; i++)
	{
		const FormField& field = g_formFields[(size_t)i];
		const float fy = y + 104 + (float)i * rowH;
		const bool selected = (!pointerOn() && !g_keyboard && g_formSel == i) || hovering(x + 40, fy + 30, w - 80, 56);
		gfx::text(x + 44, fy, 21, kMuted, field.hint.empty() ? field.label : field.label + "   (" + field.hint + ")", w - 90);
		gfx::panel(x + 40, fy + 30, w - 80, 56, 12, selected ? 2.0f : 1.0f, selected ? kBorder : kLine, kDeep);
		gfx::text(x + 60, fy + 44, 25, kText, field.value, w - 120);
		clickable(x + 40, fy + 30, w - 80, 56, [i] { editField(i); });
	}

	const float by = y + h - 104;
	if (!g_formError.empty())
		gfx::text(x + 44, by - 34, 21, kLive, g_formError, w - 90);
	static const char* const labels[2] = { "Save and open", "Cancel" };
	for (int b = 0; b < 2; b++)
	{
		const float bx = x + w - 40 - (float)(2 - b) * 250 + (float)b * 10, bw = 240;
		const bool selected = (!pointerOn() && !g_keyboard && g_formSel == fields + b) || hovering(bx, by, bw, 62);
		gfx::panel(bx, by, bw, 62, 14, 2, selected ? kBorder : kLine, b == 0 ? kSelected : kTile);
		centredTight(bx, bw, by + 18, 24, selected ? kAccent : kText, labels[b]);
		if (b == 0)
			clickable(bx, by, bw, 62, [] { submitForm(); });
		else
			clickable(bx, by, bw, 62, [] { g_form = false; });
	}
	gfx::text(x + 44, by + 20, 20, kMuted, "\xE2\x9C\x95 Edit / Choose    \xE2\x97\x8B Cancel", 520);
}

static void formInput(Action action)
{
	const int fields = (int)g_formFields.size(), count = fields + 2;
	switch (action)
	{
	case A_UP: g_formSel = (g_formSel + count - 1) % count; break;
	case A_DOWN: g_formSel = (g_formSel + 1) % count; break;
	case A_LEFT: if (g_formSel == fields + 1) g_formSel = fields; break;
	case A_RIGHT: if (g_formSel == fields) g_formSel = fields + 1; break;
	case A_OK:
		if (g_formSel < fields)
			editField(g_formSel);
		else if (g_formSel == fields)
			submitForm();
		else
			g_form = false;
		break;
	case A_BACK: g_form = false; break;
	default: break;
	}
}

