// ---- input -----------------------------------------------------------------

enum Action { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_PAGE_UP, A_PAGE_DOWN, A_MENU, A_FAVOURITE, A_SEARCH, A_TAB_PREV, A_TAB_NEXT, A_MORE, A_LAST };

static std::vector<Action> g_actions;
static bool g_padDir[4], g_stickDir[4], g_keyDir[4];   // up, down, left, right
static bool g_heldBefore[4];
static double g_repeatAt[4];
static bool g_haveController = false;
static int g_rawLogged = 0, g_buttonsLogged = 0;

static double nowSeconds()
{
	return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void openPad(int index)
{
	if (SDL_IsGameController(index))
	{
		if (SDL_GameControllerOpen(index))
		{
			g_haveController = true;
			logLine("input: controller %d opened: %s", index, SDL_GameControllerNameForIndex(index));
		}
	}
	else if (SDL_JoystickOpen(index))
		logLine("input: joystick %d opened (no standard button layout known): %s", index, SDL_JoystickNameForIndex(index));
}

static float g_stickX = 0, g_stickY = 0;      // left stick: moves the pointer
static void stick(int axis, int value)
{
	if (axis == 0) g_stickX = (float)value / 32767.0f;
	if (axis == 1) g_stickY = (float)value / 32767.0f;
}

// right stick: scrolls the list (how far it is pushed sets the speed)
static float g_scrollStick = 0, g_scrollCarry = 0;
static int g_axisLogged = 0;

// How many rows to move this frame.
static int scrollSteps(double seconds)
{
	const float push = g_scrollStick < 0 ? -g_scrollStick : g_scrollStick;
	if (push < 0.25f)
	{
		g_scrollCarry = 0;
		return 0;
	}
	const float t = ((push > 1.0f ? 1.0f : push) - 0.25f) / 0.75f;
	const float rowsPerSecond = 3.0f + 42.0f * t * t;
	g_scrollCarry += (g_scrollStick < 0 ? -1.0f : 1.0f) * rowsPerSecond * (float)seconds;
	const int steps = (int)g_scrollCarry;
	g_scrollCarry -= (float)steps;
	return steps;
}

static void onEvent(const SDL_Event& e, bool& running)
{
	switch (e.type)
	{
	case SDL_QUIT:
		running = false;
		break;
	case SDL_CONTROLLERDEVICEADDED:
		openPad(e.cdevice.which);
		break;
	case SDL_JOYDEVICEADDED:
		if (!SDL_IsGameController(e.jdevice.which))
			openPad(e.jdevice.which);
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
	{
		const bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
		if (down && g_buttonsLogged < 30)
		{
			g_buttonsLogged++;
			logLine("input: controller button %d pressed (Options is %d)", (int)e.cbutton.button, (int)SDL_CONTROLLER_BUTTON_START);
		}
		switch (e.cbutton.button)
		{
		case SDL_CONTROLLER_BUTTON_DPAD_UP: g_padDir[0] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_DOWN: g_padDir[1] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_LEFT: g_padDir[2] = down; break;
		case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: g_padDir[3] = down; break;
		case SDL_CONTROLLER_BUTTON_A: if (down) g_actions.push_back(A_OK); break;
		case SDL_CONTROLLER_BUTTON_B: if (down) g_actions.push_back(A_BACK); break;
		case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: if (down) g_actions.push_back(A_PAGE_UP); break;
		case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: if (down) g_actions.push_back(A_PAGE_DOWN); break;
		case SDL_CONTROLLER_BUTTON_START:      // Options
		case SDL_CONTROLLER_BUTTON_BACK:       // Create / Share
		case 20:                               // the touchpad click, on SDL versions that report it
			if (down) g_actions.push_back(A_MENU);
			break;
		case SDL_CONTROLLER_BUTTON_X: if (down) g_actions.push_back(A_FAVOURITE); break;
		case SDL_CONTROLLER_BUTTON_Y: if (down) g_actions.push_back(A_SEARCH); break;
		case SDL_CONTROLLER_BUTTON_RIGHTSTICK: if (down) g_actions.push_back(A_MORE); break;    // R3
		case SDL_CONTROLLER_BUTTON_LEFTSTICK: if (down) g_actions.push_back(A_LAST); break;     // L3
		default: break;
		}
		break;
	}
	case SDL_CONTROLLERAXISMOTION:
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) stick(0, e.caxis.value);
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) stick(1, e.caxis.value);
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) g_scrollStick = (float)e.caxis.value / 32767.0f;
		if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
		{
			// L2 / R2: one press each time the trigger is pulled past half-way
			static bool pulled[2] = { false, false };
			const int side = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1 : 0;
			const bool now = e.caxis.value > 16000;
			if (now && !pulled[side])
				g_actions.push_back(side ? A_TAB_NEXT : A_TAB_PREV);
			pulled[side] = now;
			break;
		}
		if (e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX && e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY && g_axisLogged < 4 && (e.caxis.value > 20000 || e.caxis.value < -20000))
		{
			g_axisLogged++;
			logLine("input: controller axis %d moved (right stick up/down is axis %d)", (int)e.caxis.axis, (int)SDL_CONTROLLER_AXIS_RIGHTY);
		}
		break;

	// A pad SDL has no layout for: a best guess, and the first presses are logged so it can be corrected.
	case SDL_JOYBUTTONDOWN:
		if (g_haveController)
			break;
		if (g_rawLogged++ < 40)
			logLine("input: raw joystick button %d", (int)e.jbutton.button);
		if (e.jbutton.button == 0) g_actions.push_back(A_OK);
		else if (e.jbutton.button == 1) g_actions.push_back(A_BACK);
		else if (e.jbutton.button == 4) g_actions.push_back(A_PAGE_UP);
		else if (e.jbutton.button == 5) g_actions.push_back(A_PAGE_DOWN);
		break;
	case SDL_JOYHATMOTION:
		if (g_haveController)
			break;
		g_padDir[0] = (e.jhat.value & SDL_HAT_UP) != 0;
		g_padDir[1] = (e.jhat.value & SDL_HAT_DOWN) != 0;
		g_padDir[2] = (e.jhat.value & SDL_HAT_LEFT) != 0;
		g_padDir[3] = (e.jhat.value & SDL_HAT_RIGHT) != 0;
		break;
	case SDL_JOYAXISMOTION:
		if (!g_haveController && e.jaxis.axis < 2)
			stick(e.jaxis.axis, e.jaxis.value);
		else if (!g_haveController && (e.jaxis.axis == 3 || e.jaxis.axis == 4))
			g_scrollStick = (float)e.jaxis.value / 32767.0f;
		break;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
	{
		const bool down = e.type == SDL_KEYDOWN;
		switch (e.key.keysym.sym)
		{
		case SDLK_UP: g_keyDir[0] = down; break;
		case SDLK_DOWN: g_keyDir[1] = down; break;
		case SDLK_LEFT: g_keyDir[2] = down; break;
		case SDLK_RIGHT: g_keyDir[3] = down; break;
		case SDLK_RETURN: if (down && !e.key.repeat) g_actions.push_back(A_OK); break;
		case SDLK_ESCAPE:
		case SDLK_BACKSPACE: if (down && !e.key.repeat) g_actions.push_back(A_BACK); break;
		case SDLK_PAGEUP: if (down) g_actions.push_back(A_PAGE_UP); break;
		case SDLK_PAGEDOWN: if (down) g_actions.push_back(A_PAGE_DOWN); break;
		case SDLK_f: if (down && !e.key.repeat) g_actions.push_back(A_FAVOURITE); break;
		case SDLK_s: if (down && !e.key.repeat) g_actions.push_back(A_SEARCH); break;
		case SDLK_m: if (down && !e.key.repeat) g_actions.push_back(A_MORE); break;
		case SDLK_l: if (down && !e.key.repeat) g_actions.push_back(A_LAST); break;
		case SDLK_r: if (down && !e.key.repeat) g_actions.push_back(A_MENU); break;
		case SDLK_LEFTBRACKET: if (down && !e.key.repeat) g_actions.push_back(A_TAB_PREV); break;
		case SDLK_RIGHTBRACKET: if (down && !e.key.repeat) g_actions.push_back(A_TAB_NEXT); break;
		default: break;
		}
		break;
	}
	default:
		break;
	}
}

// ---- pointer ---------------------------------------------------------------
// The left stick moves a pointer; Cross clicks whatever is under it. Everything
// that can be clicked registers its area while it is being drawn.

struct HitArea { float x, y, w, h; std::function<void()> click; };
static std::vector<HitArea> g_hits;
static float g_pointerX = 960, g_pointerY = 540;
static double g_pointerUntil = 0;              // shown until this time

static bool pointerOn()
{
	return nowSeconds() < g_pointerUntil;
}

// While drawing is limited to a box (a list scrolling under a heading), the pointer is limited to
// it too: what has scrolled out of the box cannot be seen, so it cannot be pointed at or clicked.
static bool g_clipOn = false;
static float g_clipBox[4] = { 0, 0, 0, 0 };

static void clipTo(float x, float y, float w, float h)
{
	gfx::clip(x, y, w, h);
	g_clipOn = true;
	g_clipBox[0] = x; g_clipBox[1] = y; g_clipBox[2] = w; g_clipBox[3] = h;
}

static void unclipAll()
{
	gfx::unclip();
	g_clipOn = false;
}

static bool hovering(float x, float y, float w, float h)
{
	if (g_clipOn && (g_pointerX < g_clipBox[0] || g_pointerX >= g_clipBox[0] + g_clipBox[2] || g_pointerY < g_clipBox[1] || g_pointerY >= g_clipBox[1] + g_clipBox[3]))
		return false;
	return pointerOn() && g_pointerX >= x && g_pointerX < x + w && g_pointerY >= y && g_pointerY < y + h;
}

static void clickable(float x, float y, float w, float h, std::function<void()> click)
{
	if (g_clipOn)
	{
		// only the part inside the box
		const float x2 = x + w < g_clipBox[0] + g_clipBox[2] ? x + w : g_clipBox[0] + g_clipBox[2], y2 = y + h < g_clipBox[1] + g_clipBox[3] ? y + h : g_clipBox[1] + g_clipBox[3];
		if (x < g_clipBox[0]) x = g_clipBox[0];
		if (y < g_clipBox[1]) y = g_clipBox[1];
		w = x2 - x;
		h = y2 - y;
		if (w <= 0 || h <= 0)
			return;
	}
	g_hits.push_back({ x, y, w, h, std::move(click) });
}

static void movePointer(double seconds)
{
	const float length = sqrtf(g_stickX * g_stickX + g_stickY * g_stickY);
	if (length < 0.2f)
		return;
	// slow near the centre for small targets, fast at full tilt to cross the screen
	const float push = (length > 1.0f ? 1.0f : length) - 0.2f;
	const float level = (float)g_pointerSpeed;
	const float speed = 45.0f * level + 215.0f * level * (push / 0.8f) * (push / 0.8f);
	g_pointerX += g_stickX / length * speed * (float)seconds;
	g_pointerY += g_stickY / length * speed * (float)seconds;
	if (g_pointerX < 0) g_pointerX = 0;
	if (g_pointerX > 1919) g_pointerX = 1919;
	if (g_pointerY < 0) g_pointerY = 0;
	if (g_pointerY > 1079) g_pointerY = 1079;
	g_pointerUntil = nowSeconds() + 6;
}

// Clicks the topmost area under the pointer. Returns false if there is none.
static bool clickPointer()
{
	for (size_t i = g_hits.size(); i-- > 0;)
	{
		const HitArea& area = g_hits[i];
		if (g_pointerX >= area.x && g_pointerX < area.x + area.w && g_pointerY >= area.y && g_pointerY < area.y + area.h)
		{
			const std::function<void()> click = area.click; // the list may change while it runs
			g_pointerUntil = nowSeconds() + 6;
			if (click)
				click();
			return true;
		}
	}
	return false;
}

static void drawPointer()
{
	if (pointerOn())
		drawPointerShape(g_pointerType, g_pointerX, g_pointerY);
}

// Held directions repeat, slowly at first and then quickly, for long lists.
static void repeatDirections()
{
	static const Action actions[4] = { A_UP, A_DOWN, A_LEFT, A_RIGHT };
	const double now = nowSeconds();
	for (int i = 0; i < 4; i++)
	{
		const bool held = g_padDir[i] || g_stickDir[i] || g_keyDir[i];
		if (g_padDir[i] || g_keyDir[i])
			g_pointerUntil = 0; // using the direction buttons puts the pointer away
		if (held && !g_heldBefore[i])
		{
			g_actions.push_back(actions[i]);
			g_repeatAt[i] = now + 0.38;
		}
		else if (held && now >= g_repeatAt[i])
		{
			g_actions.push_back(actions[i]);
			g_repeatAt[i] = now + 0.055;
		}
		g_heldBefore[i] = held;
	}
}

