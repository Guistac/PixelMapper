

1. artnet network protocol
2. user input (imgui)
3. renderer (imgui)

```c++
// render viewport + render artnet
usize total_universe = 5;
usize pixel_count = total_pixel;

struct Pixel {
	vec2 pos;
	u32 color;
};


usize i = 0;
for (all universe) {
	u8 *buf = dmx_buffer[i * 512];
	artnet_frame(buf);
	i += 1;
}

struct Universe {
	bool used;
	u8 buffer[512];
};

struct ArtnetEndpoint {
};

struct Controler {
	u32 ipv4;
	usize start_universe, end_universe;
};

struct Patch {
	// Serializable
	Fixture fixtures[];
	Controler controlers[];

	void serialize();
	bool deserialize();

	// Runtime
	Universe universe[256];
	Pixel pixels[];
};

struct State {
	gui;
	window;
	udp;
};


void buildPixelList();

load();
buildPixelList();

main_loop {
	guiBegin();
		if (button) save();
	guiEnd();
	if (dirty) {
		buildPixelList();
	}
	renderPixels();
	encodeAndSendArtnet();
}

// GUI
struct GuiFixture {
	usize id;
	vec2 start, end;
	usize start_addr, start_universe;
	//Pixel pixels[];
};


```

1. GUI place and configure fixture (pos, size, dmx addr)
2. Render pixels colors
3. Encode and send dmx (artnet) frame
