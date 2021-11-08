#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <PL/platform_filesystem.h>
#include <PL/platform_math.h>

#define VERSION "0.02"

/* debug flags */
#define DEBUG_PARSER

enum { /* do NOT change the ordering of these!!! */
    MAP_FORMAT_IDT2,    /* Quake, Quake 2 */
    MAP_FORMAT_IDT3,    /* Quake 3 */
    MAP_FORMAT_IDT4,    /* Doom 3 */

    MAP_FORMAT_GSRC,    /* Half-Life */
    MAP_FORMAT_SRC,     /* Half-Life 2 */

    MAX_MAP_FORMATS
};

unsigned int startup_format = MAP_FORMAT_IDT2;

bool startup_test = false;
bool startup_actors = false;
bool startup_add = false;
bool startup_sub = false;

void GameCommand(const char *parm) {
    if(strncmp("idt2", parm, 4) == 0) { /* this is the default */
        return;
    }

    if(strncmp("idt3", parm, 4) == 0) {
        startup_format = MAP_FORMAT_IDT3;
    } else if(strncmp("idt4", parm, 4) == 0) {
        startup_format = MAP_FORMAT_IDT4;
    } else if(strncmp("gsrc", parm, 4) == 0) {
        startup_format = MAP_FORMAT_GSRC;
    } else if(strncmp("src", parm, 3) == 0) {
        startup_format = MAP_FORMAT_SRC;
    }
}

/**************************************************/

/* https://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both */
void ConvertHSV(unsigned char h, unsigned char s, unsigned char v,
                unsigned char *r, unsigned char *g, unsigned char *b) {
    if(s == 0) {
        if(v == 0) {
            *r = 255;
            *g = 255;
            *b = 255;
        } else {
            *r = v;
            *g = v;
            *b = v;
        }
        return;
    }

    unsigned char region = (unsigned char) (h / 43);
    unsigned char remainder = (unsigned char) ((h - (region * 43)) * 6);

    unsigned char p = (unsigned char) ((v * (255 - s)) >> 8);
    unsigned char q = (unsigned char) ((v * (255 - ((s * remainder) >> 8))) >> 8);
    unsigned char t = (unsigned char) ((v * (255 - ((s * (255 - remainder)) >> 8))) >> 8);

    switch(region) {
        case 0:     *r = v; *g = t; *b = p; break;
        case 1:     *r = q; *g = v; *b = p; break;
        case 2:     *r = p; *g = v; *b = t; break;
        case 3:     *r = p; *g = q; *b = v; break;
        case 4:     *r = t; *g = p; *b = v; break;
        default:    *r = v; *g = p; *b = q; break;
    }
}

/**************************************************/

#define MAX_MAP_BRUSHES     56875
#define MAX_MAP_ENTITIES    4096

#define MAX_BRUSH_FACES 4096

enum {
    CTX_MAP,

    CTX_ACTORLIST,
    CTX_ACTOR,

    CTX_BRUSH,

    CTX_POLYLIST,
    CTX_POLYGON,
};

/****************************
 * Actors
 ***************************/

enum {
    ACT_Brush,          /* */
    ACT_Mover,          /* func_wall */

    ACT_AmbientSound,   /* */

    ACT_PlayerStart,    /* info_player_start */

    ACT_Light,          /* light */
    ACT_Spotlight,      /* light_spot */

    ACT_Sparks,         /* env_spark */

    ACT_LevelSummary,   /* worldspawn */
    ACT_LevelInfo,      /* worldspawn */

    ACT_PathNode,

    /* UT99 */
    ACT_HealthVial,     /* item_battery */

    ACT_Unknown
};

#define StandardEntityName(a)                   { (a), (a), (a), (a), (a) }
#define EntityName(ID2, ID3, ID4, GSRC, SRC)    { (ID2), (ID3), (ID4), (GSRC), (SRC) }

typedef struct ActorDef {
    const char *name;
    unsigned int id;

    const char *targets[MAX_MAP_FORMATS];
} ActorDef;
ActorDef actor_definitions[]={
        {"Brush",           ACT_Brush},
        {"Mover",           ACT_Mover},
        {"AmbientSound",    ACT_AmbientSound},

        { "PlayerStart",    ACT_PlayerStart,    StandardEntityName("info_player_start") },
        { "Light",          ACT_Light,          StandardEntityName("light") },

        { "PathNode",       ACT_PathNode,       EntityName(NULL, NULL, NULL, "info_node", "info_node") },

        {"Spotlight",       ACT_Spotlight},
        {"Sparks",          ACT_Sparks},
        {"LevelSummary",    ACT_LevelSummary},
        {"LevelInfo",       ACT_LevelInfo},

        /* UT99 */
        { "HealthVial",     ACT_HealthVial,     EntityName(NULL, NULL, NULL, "item_battery", NULL) },

        {NULL}
};

typedef struct Actor {
    char name[64];
    char class[64];

    ActorDef *class_index;

    PLVector3 location;

    union {
        struct {
            char effect[32];

            /* HSV                                 */
            unsigned int hue;           /* 0 - 255 */
            unsigned int saturation;    /* 0 - 255 */
            unsigned int brightness;    /* 0 - 255 */

            unsigned int radius;
        } Light;

        struct {
            float time;
            float day_fraction;

            unsigned int ticks;
            unsigned int year;
            unsigned int month;
            unsigned int day;
            unsigned int hour;
            unsigned int second;
            unsigned int millisecond;
        } LevelDescriptor;

        struct {
            char title[32];
            char author[32];
            char player_count[32];
            char level_enter_text[32];

            int recommended_enemies;
            int recommended_teammates;
        } LevelSummary; /* + LevelInfo */

        struct {
            char csg[32];
        } Brush;
    };
} Actor;

ActorDef *GetActorIdentification(const char *name) {
    for(unsigned int i = 0; i < plArrayElements(actor_definitions); ++i) {
        if(actor_definitions[i].name == NULL) {
            break;
        }

        if(pl_strncasecmp(name, actor_definitions[i].name, sizeof(name)) == 0) {
            return &actor_definitions[i];
        }
    }

    return &actor_definitions[ACT_Unknown];
}

const char *GetEntityForActor(Actor *actor) {
    if(startup_actors || actor->class_index->id == ACT_Unknown) {
        if(actor->class[0] == '\0' || actor->class[0] == ' ') {
            printf("warning: invalid actor name, possibly failed to parse?\n");
            return "unknown";
        }
        return &actor->class[0];
    }

    const char *target = actor->class_index->targets[startup_format];
    if(target == NULL || target[0] == '\0') {
        printf("warning: no entity target provided for actor \"%s\" in this mode, returning actor name instead!\n",
               actor->class_index->name);
        return &actor->class[0];
    }

    return target;
}

/****************************/

typedef struct Polygon { /* i 'ssa face >:I */
    char item[64];
    char texture[128];
    char group[64];

    /* T3D spec specifies that we're not guaranteed four vertices
     * so, while I haven't seen a case of this, we will respect it */
    PLVector3 vertices[32];
    unsigned int num_vertices;

    PLVector3 u;
    PLVector3 v;

    PLVector3 origin;
} Polygon;

enum {
    CSG_Active,
    CSG_Add,
    CSG_Subtract,
    CSG_Intersect,
    CSG_Deintersect,
};

typedef struct Brush { /* i 'ssa primitive >:I */
    char name[32];

    Polygon *poly_list;
    Polygon *cur_poly;
    unsigned int num_poly;
    unsigned int max_poly;

    PLVector3 location;
    PLVector3 rotation;
    PLVector3 pre_pivot;
    PLVector3 post_pivot;

    unsigned int csg;
    unsigned int flags;
    unsigned int poly_flags;
    unsigned int colour;
} Brush;

struct {
    struct {
        char name[32];

        unsigned int num_brushes;
    } map;

    Brush brushes[MAX_MAP_BRUSHES];
    Brush *cur_brush;
    unsigned int num_brushes;

    Actor actors[MAX_MAP_ENTITIES];
    Actor *cur_actor;
    unsigned int num_actors;

    /* parsing data */

    struct {
        unsigned int context;
    } chunks[16];
    int cur_chunk;
    char *cur_pos;

    unsigned int cur_line;
} t3d;

void print_state(void) {
  printf(
      "t3d state\n"
      "cur_line=%d\n"
      "cur_chunk=%d\n"
      "num_actors=%d\n"
      "num_brushes=%d\n"
      "context=%d\n",

      t3d.cur_line,
      t3d.cur_chunk,
      t3d.num_actors,
      t3d.num_brushes,
      t3d.chunks[t3d.cur_chunk].context
  );
}

/* yeah, yeah... I know... shut-up. */
#define SkipLine()      while(*t3d.cur_pos != '\n' && *t3d.cur_pos != '\r') { t3d.cur_pos++; } t3d.cur_line++;
#define SkipSpaces()    while(*t3d.cur_pos == ' ') { t3d.cur_pos++; }
#define ParseBlock()    while(*t3d.cur_pos != '\0')
#define ParseLine()     while(*t3d.cur_pos != '\0' && *t3d.cur_pos != '\n' && *t3d.cur_pos != '\r')

void ParseString(char *out, size_t len) {
    SkipSpaces();
    if(*t3d.cur_pos == '=') t3d.cur_pos++;
    SkipSpaces();

    unsigned int i = 0;
    ParseBlock() {
        if(*t3d.cur_pos == ' ' || *t3d.cur_pos == '\n' || *t3d.cur_pos == '\r') {
            break;
        }

        assert(i < len);
        if(i >= len) {
          printf("Failed to parse entire string, buffer is too small!\n");
        }

        out[i++] = *t3d.cur_pos++;
    }
    out[i] = '\0';

#ifdef DEBUG_PARSER
    printf("value=%s\n", out);
#endif
}

void ParseNext(void) {
    ParseBlock() {
        if (*t3d.cur_pos == '\n' || *t3d.cur_pos == '\r') {
            if(t3d.cur_pos[0] == '\r' && t3d.cur_pos[1] == '\n') { /* LF */
                t3d.cur_line++;
                t3d.cur_pos += 2;
                continue;
            } else {
                printf("funny line ending... blurgh!\n");
            }

            t3d.cur_pos++;
            continue;
        }

        if(*t3d.cur_pos == '\t') {
            t3d.cur_pos++;
            continue;
        }

        if(*t3d.cur_pos == ' ') {
            SkipSpaces();
            continue;
        }

        if (*t3d.cur_pos == ';') { /* skip comment */
            SkipLine();
            continue;
        }

        break;
    }
}

int ParseInteger(void) {
    char n[11];
    ParseString(n, sizeof(n));
#ifdef DEBUG_PARSER
  printf("value=%s\n", n);
#endif
    return atoi(n);
}

float ParseVectorCoordinate(void) {
    while(*t3d.cur_pos != '+' && *t3d.cur_pos != '-' && isdigit(*t3d.cur_pos) == 0) t3d.cur_pos++;
    float f = strtof(t3d.cur_pos, &t3d.cur_pos);
    if (*t3d.cur_pos == ',') t3d.cur_pos++;
    return f;
}

PLVector3 ParseVector(void) {
    if(*t3d.cur_pos == '(') t3d.cur_pos++;

    PLVector3 vector = {
        ParseVectorCoordinate(),
        ParseVectorCoordinate(),
        ParseVectorCoordinate()
    };

    SkipLine();

#ifdef DEBUG_PARSER
  printf("value=%s\n", plPrintVector3(&vector, pl_int_var));
#endif

    return vector;
}

bool ChunkStart(void) {
    if(pl_strncasecmp(t3d.cur_pos, "Begin", 5) == 0) {
        t3d.cur_pos += 5;
        ParseNext();
        return true;
    }

    return false;
}

bool ChunkEnd(const char *chunk) {
    if(pl_strncasecmp(t3d.cur_pos, "End", 3) == 0) {
        t3d.cur_pos += 3;
        ParseNext();

        size_t len = strlen(chunk);
        if(pl_strncasecmp(t3d.cur_pos, chunk, len) != 0) {
            printf("error: missing end segment for %s!\n", chunk);
            print_state();
            exit(EXIT_FAILURE);
        }

        SkipLine();
        ParseNext();

        return true;
    }

    return false;
}

/*******************************/

void ReadMap();
void ReadBrush();
void ReadPolyList();
void ReadPolygon();
void ReadActor();

bool ReadField(const char *prop) {
    SkipSpaces();

#ifdef DEBUG_PARSER
  printf("prop=%s\n", prop);
#endif

    size_t len = strlen(prop);
    if(pl_strncasecmp(t3d.cur_pos, prop, len) == 0) {
        t3d.cur_pos += len;
        SkipSpaces();
        if(*t3d.cur_pos == '=') t3d.cur_pos++;
        SkipSpaces();
        return true;
    }

    return false;
}

bool ReadVectorField(const char *prop, PLVector3 *vector) {
    if(!ReadField(prop)) {
        return false;
    }

    SkipSpaces();
    *vector = ParseVector();
    SkipLine();

    return true;
}

bool ReadProperty(const char *parm) {
    SkipSpaces();

#ifdef DEBUG_PARSER
  printf("prop=%s\n", parm);
#endif

    char prop[16];
    snprintf(prop, sizeof(prop), "%s=", parm);

    size_t len = strlen(prop);
    if(pl_strncasecmp(t3d.cur_pos, prop, len) == 0) {
        t3d.cur_pos += len;
        return true;
    }

    return false;
}

bool ReadPropertyString(const char *parm, char *out, size_t len) {
    if(ReadProperty(parm)) {
        ParseString(out, len);
        return true;
    }

    return false;
}

bool ReadPropertyInteger(const char *parm, int *out) {
    if(ReadProperty(parm)) {
        *out = ParseInteger();
        return true;
    }

    return false;
}

void SkipProperty(void) {
    char name[16];
    unsigned int pos = 0;
    while(*t3d.cur_pos != '=') {
        name[pos++] = *t3d.cur_pos++;
    }
    name[pos] = '\0';
    printf("unknown property \"%s\", ignoring!\n", name);
    while(*t3d.cur_pos != ' ' && *t3d.cur_pos != '\n' && *t3d.cur_pos != '\r') t3d.cur_pos++;
}

void ReadChunk(void) {
    t3d.cur_chunk++;

    ParseBlock() {
        if (pl_strncasecmp(t3d.cur_pos, "Map", 3) == 0) {
            t3d.chunks[t3d.cur_chunk].context = CTX_MAP;
            t3d.cur_pos += 3;
            ReadMap();
            break;
        }

        if (pl_strncasecmp(t3d.cur_pos, "Brush", 5) == 0) {
            t3d.cur_pos += 5;
            ReadBrush();
            break;
        }

        if (pl_strncasecmp(t3d.cur_pos, "Actor", 5) == 0) {
            t3d.cur_pos += 5;
            ReadActor();
            break;
        }

        if (pl_strncasecmp(t3d.cur_pos, "PolyList", 8) == 0) {
            t3d.cur_pos += 8;
            ReadPolyList();
            break;
        }

        if (pl_strncasecmp(t3d.cur_pos, "Polygon", 7) == 0) {
            t3d.cur_pos += 7;
            ReadPolygon();
            break;
        }

        if (pl_strncasecmp(t3d.cur_pos, "ActorList", 9) == 0) {
            t3d.chunks[t3d.cur_chunk].context = CTX_ACTORLIST;
            SkipLine();
            break;
        }

        char chunk_name[32];
        ParseString(chunk_name, 32);
        printf("unhandled chunk \"%s\"!\n", chunk_name);
        exit(EXIT_FAILURE);
    }
    t3d.cur_chunk--;
}

#define print_heading(a)    for(unsigned int i = 0; i < t3d.cur_chunk; ++i) printf(" "); printf("parsing " a "\n");

void ReadPolygon(void) {
    print_heading("Polygon");

    ParseLine() {
        Polygon* polygon = t3d.cur_brush->cur_poly;
        if(polygon == NULL) {
          printf("Invalid polygon handle encountered, aborting!\n");
          print_state();
          exit(EXIT_FAILURE);
        }

        if(ReadPropertyString("Item", polygon->item, sizeof(polygon->item))) {
            continue;
        }

        if(ReadPropertyString("Texture", polygon->texture, sizeof(polygon->texture))) {
            continue;
        }

        if(ReadPropertyString("Group", t3d.cur_brush->cur_poly->group, sizeof(polygon->group))) {
            continue;
        }

        /* T3D spec suggests to ignore 'link' property, so we shall */

        SkipProperty();
    }

    if(plIsEmptyString(t3d.cur_brush->cur_poly->texture)) {
        strncpy(t3d.cur_brush->cur_poly->texture, "none", sizeof(t3d.cur_brush->cur_poly->texture));
    }

    ParseBlock() {
        ParseNext();

        if(ChunkEnd("Polygon")) {
            break;
        }

        if(ChunkStart()) {
            ReadChunk();
            continue;
        }

        if(ReadVectorField("Origin", &t3d.cur_brush->cur_poly->origin)) {
            continue;
        }

        if(ReadVectorField("TextureU", &t3d.cur_brush->cur_poly->u)) {
            continue;
        }

        if(ReadVectorField("TextureV", &t3d.cur_brush->cur_poly->v)) {
            continue;
        }

        if(ReadVectorField("Vertex", &t3d.cur_brush->cur_poly->vertices[t3d.cur_brush->cur_poly->num_vertices])) {
            t3d.cur_brush->cur_poly->num_vertices++;
            continue;
        }

        SkipLine();
    }

    t3d.cur_brush->cur_poly++;
    t3d.cur_brush->num_poly++;
    if(t3d.cur_brush->num_poly > t3d.cur_brush->max_poly) {
        printf("error: invalid number of polygons for brush (%d / %d), aborting!\n",
               t3d.cur_brush->num_poly, t3d.cur_brush->max_poly);
        exit(EXIT_FAILURE);
    }
}

void ReadPolyList(void) {
    print_heading("PolyList");

    ParseLine() {
        if(ReadPropertyInteger("Num", (int *) &t3d.cur_brush->max_poly)) {
            continue;
        }

        SkipProperty();
    }

    if(t3d.cur_brush->max_poly == 0) {
        t3d.cur_brush->max_poly = MAX_BRUSH_FACES;
    }

    if((t3d.cur_brush->poly_list = calloc(t3d.cur_brush->max_poly, sizeof(Polygon))) == NULL) {
        printf("error: failed to allocate %d polygons, aborting!\n", t3d.cur_brush->num_poly);
        print_state();
        exit(EXIT_FAILURE);
    }

    t3d.cur_brush->cur_poly = &t3d.cur_brush->poly_list[0];

    ParseBlock() {
        ParseNext();

        if(ChunkEnd("PolyList")) {
            break;
        }

        if(ChunkStart()) {
            ReadChunk();
            continue;
        }

        SkipLine();
    }

    if(t3d.cur_brush->num_poly < t3d.cur_brush->max_poly) {
        printf("shrinking polylist (%d < %d)... ", t3d.cur_brush->num_poly, t3d.cur_brush->max_poly);
        if((t3d.cur_brush->poly_list = realloc(t3d.cur_brush->poly_list, t3d.cur_brush->num_poly * sizeof(Polygon)))
           == NULL) {
            printf("error: failed to shrink polylist down!\n");
            print_state();
            exit(EXIT_FAILURE);
        }
        t3d.cur_brush->max_poly = t3d.cur_brush->num_poly;
        printf("done!\n");
    }
}

void ReadMap(void) {
    print_heading("Map");

    /* header */
    ParseLine() {
        if(ReadPropertyString("Name", t3d.map.name, sizeof(t3d.map.name))) {
            continue;
        }

        if(ReadPropertyInteger("Brushes", (int*) &t3d.map.num_brushes)) {
            continue;
        }

        SkipProperty();
    }

    ParseBlock() {
        ParseNext();

        if(ChunkEnd("Map")) {
            break;
        }

        if(ChunkStart()) {
            ReadChunk();
            continue;
        }

        SkipLine();
    }
}

void ReadActor(void) {
    t3d.chunks[t3d.cur_chunk].context = CTX_ACTOR;

    print_heading("Actor");

    Actor* actor = t3d.cur_actor;
    if(actor == NULL) {
      printf("Invalid actor handle encountered, aborting!\n");
      print_state();
      exit(EXIT_FAILURE);
    }

    ParseLine() {
        if(ReadPropertyString("Class", actor->class, sizeof(actor->class))) {
            actor->class_index = GetActorIdentification(actor->class);
            continue;
        }

        if(ReadPropertyString("Name", actor->name, sizeof(actor->name))) {
            continue;
        }

        SkipProperty();
    }

    ParseBlock() {
        ParseNext();

        if(ChunkEnd("Actor")) {
            break;
        }

        if(ChunkStart()) {
            ReadChunk();
            continue;
        }

        if(ReadVectorField("Location", &actor->location)) {
            continue;
        }

        switch(actor->class_index->id) {
            default:break;

            case ACT_LevelSummary:break;
            case ACT_Spotlight:break;

            case ACT_Light: {
                if(ReadPropertyString("LightEffect", actor->Light.effect, sizeof(actor->Light.effect))) {
                    continue;
                }

                if(ReadPropertyInteger("LightBrightness", (int *) &actor->Light.brightness)) {
                    continue;
                }

                if(ReadPropertyInteger("LightHue", (int *) &actor->Light.hue)) {
                    continue;
                }

                if(ReadPropertyInteger("LightRadius", (int *) &actor->Light.radius)) {
                    continue;
                }

                if(ReadPropertyInteger("LightSaturation", (int *) &actor->Light.saturation)) {
                    continue;
                }
            } break;

            case ACT_Brush: {
                if(ReadPropertyString("CsgOper", actor->Brush.csg, sizeof(actor->Brush.csg))) {
                    continue;
                }
            } break;
        }

        SkipLine();
    }

    t3d.num_actors++;
    t3d.cur_actor++;
}

void ReadBrush(void) {
    t3d.chunks[t3d.cur_chunk].context = CTX_BRUSH;

    print_heading("Brush");

    ParseLine() {
        if(ReadPropertyString("Name", t3d.map.name, sizeof(t3d.map.name))) {
            continue;
        }

        SkipProperty();
    }

    if((t3d.cur_chunk > 0) && (t3d.chunks[t3d.cur_chunk - 1].context == CTX_ACTOR)) {
        if (t3d.cur_actor->class_index->id == ACT_Brush || t3d.cur_actor->class_index->id == ACT_Mover) {
            t3d.cur_brush->location = t3d.cur_actor->location;
            if (pl_strncasecmp(t3d.cur_actor->Brush.csg, "CSG_Subtract", 12) == 0) {
                t3d.cur_brush->csg = CSG_Subtract;
            } else if (pl_strncasecmp(t3d.cur_actor->Brush.csg, "CSG_Active", 10) == 0) {
                t3d.cur_brush->csg = CSG_Active;
            } else if (pl_strncasecmp(t3d.cur_actor->Brush.csg, "CSG_Add", 7) == 0) {
                t3d.cur_brush->csg = CSG_Add;
            } else if (pl_strncasecmp(t3d.cur_actor->Brush.csg, "CSG_Deintersect", 15) == 0) {
                t3d.cur_brush->csg = CSG_Deintersect;
            } else if (pl_strncasecmp(t3d.cur_actor->Brush.csg, "CSG_Intersect", 13) == 0) {
                t3d.cur_brush->csg = CSG_Intersect;
            }
        } else {
            printf("warning: previous chunk was an actor but not of a brush class!\n");
        }
    }

    ParseBlock() {
        ParseNext();

        if(ChunkEnd("Brush")) {
            break;
        }

        if(ChunkStart()) {
            ReadChunk();
            continue;
        }

        if(ReadVectorField("Location", &t3d.cur_brush->location)) {
            continue;
        }

        if(ReadVectorField("PrePivot", &t3d.cur_brush->pre_pivot)) {
            continue;
        }

        if(ReadVectorField("PostPivot", &t3d.cur_brush->post_pivot)) {
            continue;
        }

        if(ReadField("Settings")) {
            ParseLine() {
                SkipSpaces();

                if(ReadPropertyInteger("CSG", (int *) &t3d.cur_brush->csg)) {
                    continue;
                }

                if(ReadPropertyInteger("Flags", (int *) &t3d.cur_brush->flags)) {
                    continue;
                }

                if(ReadPropertyInteger("PolyFlags", (int *) &t3d.cur_brush->poly_flags)) {
                    continue;
                }

                if(ReadPropertyInteger("Color", (int *) &t3d.cur_brush->colour)) {
                    continue;
                }
            }
            continue;
        }

        SkipLine();
    }

    t3d.num_brushes++;
    t3d.cur_brush++;
}

void ParseT3D(const char *path) {
    memset(&t3d, 0, sizeof t3d);

    printf("attempting to read T3D at \"%s\" ... ", path);

    if(!plFileExists(path)) {
        printf("failed to find \"%s\", aborting!\n", path);
        exit(EXIT_FAILURE);
    }

    PLFile *fp = plOpenFile(path, false);
    if(fp == NULL) {
        printf("failed to read \"%s\", aborting!\n", path);
        exit(EXIT_FAILURE);
    }

    char *buf;
    size_t length = plGetFileSize(fp);
    buf = malloc(length);
    if(plReadFile(fp, buf, 1, length) != length) {
      printf("Failed to read entirety of T3D, expect faults!\n");
    }
    buf[length] = '\0';
    plCloseFile(fp);

    printf("success!\n");
    printf("parsing...\n");

    t3d.cur_actor = &t3d.actors[0];
    t3d.cur_brush = &t3d.brushes[0];
    t3d.cur_chunk = -1;
    t3d.cur_pos   = &buf[0];

    ParseBlock() {
        ParseNext();

        /* begin */
        if(pl_strncasecmp(t3d.cur_pos, "Begin", 5) == 0) {
            t3d.cur_pos += 5;

            ParseNext();
            ReadChunk();
            continue;
        }
    }

    if(t3d.cur_chunk != -1) {
        printf("warning: failed to escape all blocks - parsing may have failed!\n");
    }
}

void WriteMap(const char *path) {
    FILE *fp = fopen(path, "w");
    if(fp == NULL) {
        printf("failed to open \"%s\", aborting!\n", path);
        exit(EXIT_FAILURE);
    }

#define WriteField(a, b)    fprintf(fp, "\"%s\" \"%s\"\n", (a), (b))
#define WriteVector(a, b)   fprintf(fp, "\"%s\" \"%d %d %d\"\n", (a), (int)(b).y, (int)(b).x, (int)(b).z)

    /* write out the world spawn */

    fprintf(fp, "//\n");
    fprintf(fp, "// generated with t3d2map v" VERSION "\n");
    fprintf(fp, "//\n");

    fprintf(fp, "{\n");
    switch(startup_format) {
        default:
        case MAP_FORMAT_IDT2: {
            WriteField("classname", "worldspawn");
            WriteField("wad", "/gfx/base.wad");
            WriteField("worldtype", "0");
        } break;
    }

    unsigned int num_brushes = t3d.map.num_brushes;
    if(num_brushes == 0) {
        if(t3d.num_brushes == 0) {
            printf("error: no brushes from t3d!\n");
            exit(EXIT_FAILURE);
        }
        num_brushes = t3d.num_brushes - 1;
    }

    printf("writing %d brushes...\n", num_brushes);
    for(unsigned int i = 0; i < num_brushes; ++i) {
        if(startup_add && t3d.brushes[i].csg != CSG_Add) {
            continue;
        }

        if(startup_sub && t3d.brushes[i].csg != CSG_Subtract) {
            continue;
        }

#ifdef DEBUG_PARSER
        printf("brush %d\n", i);
        printf(" name:     %s\n", t3d.brushes[i].name);
        printf(" csg:      %d\n", t3d.brushes[i].csg);
        printf(" location: %s\n", plPrintVector3(&t3d.brushes[i].location, pl_int_var));
#endif

        if(t3d.brushes[i].num_poly < 4) {
            printf("warning: invalid number of polygons to produce brush (%d), skipping!\n", t3d.brushes[i].num_poly);
            continue;
        }

        fprintf(fp, "// brush %d\n", i);
        fprintf(fp, "{\n");

        for(unsigned int j = 0; j < t3d.brushes[i].num_poly; ++j) {
            Polygon *cur_face = &t3d.brushes[i].poly_list[j];

            /* todo: may need to switch these coords around depending on output... */

            float x[3], y[3], z[3];
            for(unsigned int k = 0; k < 3; ++k) {
                x[k] = cur_face->vertices[k].y + t3d.brushes[i].location.y;
                y[k] = cur_face->vertices[k].x + t3d.brushes[i].location.x;
                z[k] = cur_face->vertices[k].z + t3d.brushes[i].location.z;
            }

            fprintf(fp, "( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 1 1\n",
                    (int) x[0], (int) y[0], (int) z[0],
                    (int) x[1], (int) y[1], (int) z[1],
                    (int) x[2], (int) y[2], (int) z[2],
                    cur_face->texture
            );

#if 1
            printf(" poly %d\n", j);
            printf("  texture: %s\n", cur_face->texture);
            printf("  group:   %s\n", cur_face->group);
            printf("  item:    %s\n", cur_face->item);
            for(unsigned int k = 0; k < 4; ++k) {
                printf("  vector %d (%s)\n", k, plPrintVector3(&cur_face->vertices[k], pl_int_var));
            }
#endif
        }

        fprintf(fp, "}\n");
    }

    fprintf(fp, "}\n");

    if(t3d.num_actors > 0) {
        for (unsigned int i = 0; i < (t3d.num_actors - 1); ++i) {
            if(t3d.actors[i].class_index->id == ACT_Brush) {
                continue;
            }

            fprintf(fp, "{\n");

            WriteField("classname", GetEntityForActor(&t3d.actors[i]));
            WriteVector("origin", t3d.actors[i].location);

            if (pl_strncasecmp(t3d.actors[i].class, "light", 5) == 0) {
                unsigned char r, g, b;
                ConvertHSV((unsigned char) t3d.actors[i].Light.hue,
                           (unsigned char) t3d.actors[i].Light.saturation,
                           (unsigned char) t3d.actors[i].Light.brightness,
                           &r, &g, &b);
                fprintf(fp, "\"light\" \"%d %d %d\"\n", r, g, b);

            }

            fprintf(fp, "}\n");
        }
    }
}

/**************************************************/

int main(int argc, char **argv) {
    typedef struct Argument {
        const char *check;
        bool *boolean;
        void(*function)(const char *parm);
        const char *description;
    } Argument;
    Argument launch_arguments[]= {
            { "-test", &startup_test, NULL, "if enabled, this will simply read the input and won't export anything" },

            {
                "-game",
                NULL, GameCommand,
                "changes the type of game to export for, e.g.\n"
                " idt2 (Quake, Quake 2)\n"
                " idt3 (Quake 3)\n"
                " idt4 (Doom 3)\n"
                " gsrc (Half-Life)\n"
                " src  (Half-Life 2)"
            },

            { "-actors", &startup_actors, NULL, "retain original actor names for entities" },
            { "-add", &startup_add, NULL, "only additive geometry" },
            { "-sub", &startup_sub, NULL, "only subtractive geometry" },

            {NULL, NULL}
    };

    printf("t3d2map v" VERSION "\nDeveloped by Mark \"hogsy\" Sowden <markelswo@gmail.com>\n\n");
    if(argc < 2) {
        printf("\nusage:\n t3d2map <in> [out]\n");
        for(size_t i = 0; i < plArrayElements(launch_arguments); ++i) {
            printf("  %s : %20s\n", launch_arguments[i].check, launch_arguments[i].description);
        }
        return EXIT_SUCCESS;
    }

    char in_path[PL_SYSTEM_MAX_PATH];
    snprintf(in_path, PL_SYSTEM_MAX_PATH, "%s", argv[1]);

    char out_path[PL_SYSTEM_MAX_PATH];
    if(argc > 2 && argv[2][0] != '-') {
        snprintf(out_path, PL_SYSTEM_MAX_PATH, "%s", argv[2]);
    } else {
        char ext[32];
        plStripExtension(ext, 32, plGetFileName(in_path));
        snprintf(out_path, PL_SYSTEM_MAX_PATH, "./%s.map", ext);
    }

    /* lil slower, but simpler to implement new commands... */

    for(unsigned int i = 2; i < argc; i++) {
        if(argv[i][0] == '-') {
            for(size_t j = 0; j < plArrayElements(launch_arguments); ++j) {
                if(launch_arguments[j].check == NULL) {
                    break;
                }

                if(pl_strncasecmp(launch_arguments[j].check, argv[i], sizeof(launch_arguments[j].check)) == 0) {
                    if (launch_arguments[j].function != NULL) {
                        const char *parm = NULL;
                        if ((i + 1) < argc && argv[i + 1][0] != '-') { /* pass the next argument along */
                            parm = argv[++i];
                        }
                        launch_arguments[j].function(parm);
                    } else if(launch_arguments[j].boolean != NULL) {
                        *launch_arguments[j].boolean = true;
                    }
                    continue;
                }
            }
        }

        printf("unknown or invalid command, \"%s\", ignoring!\n", argv[i]);
    }

    ParseT3D(in_path);

    if(!startup_test) {
        WriteMap(out_path);
    }

    printf("done!\n\n");

    printf("========================================\n");
    printf(" STATISTICS FOR %s\n", pl_strtoupper(in_path));
    printf("   brushes = %d\n", t3d.num_brushes);
    printf("   actors  = %d\n", t3d.num_actors);
    printf("========================================\n");

    return EXIT_SUCCESS;
}