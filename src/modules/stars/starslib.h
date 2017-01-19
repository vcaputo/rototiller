struct universe
{
	int width, height, depth;
	struct points* points;
	struct points* iterator;
};

void new_universe( struct universe** u, int width, int height, int depth );
void new_point( struct universe* universe );

#define OPACITY_MAX 8
struct return_point
{
	int x, y;
	int opacity;
};

int process_point( struct universe *u, struct return_point *rp );

