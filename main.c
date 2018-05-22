#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <PL/platform_filesystem.h>

#define VERSION "0.01"

/* debug flags */
#define DEBUG_PARSER

enum {
    MAP_FORMAT_IDT2,    /* Quake, Quake 2 */
    MAP_FORMAT_IDT3,    /* Quake 3 */
    MAP_FORMAT_IDT4,    /* Doom 3 */

    MAP_FORMAT_GSRC,    /* Half-Life */
    MAP_FORMAT_SRC,     /* Half-Life 2 */
};

unsigned int startup_format = MAP_FORMAT_IDT2;

bool startup_test = false;
bool startup_actors = false;

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

#define MAX_MAP_BRUSHES     56875
#define MAX_MAP_ENTITIES    4096

#define MAX_BRUSH_FACES 32

enum {
    CTX_MAP,

    CTX_ACTORLIST,
    CTX_ACTOR,

    CTX_BRUSH,

    CTX_POLYLIST,
    CTX_POLYGON,
};

/* id Tech types */

typedef struct IVector {
    int x, y, z;
} IVector;

void PrintVector(IVector vector) {
    printf("vector %d %d %d\n", vector.x, vector.y, vector.z);
}

/* UE types */

typedef struct UVector {
    float x, y, z;
} UVector;

typedef struct UColor {
    char r, g, b, a;
} UColor;

/****************************
 * Actors
 ***************************/

enum {
    ACT_Brush,          /* */

    ACT_AmbientSound,   /* */

    ACT_PlayerStart,    /* info_player_start */

    ACT_Light,          /* light */
    ACT_Spotlight,      /* light_spot */

    ACT_Sparks,         /* env_spark */

    ACT_Unknown
};

typedef struct ActorDef {
    const char *name;
    unsigned int id;
} ActorDef;
ActorDef actor_definitions[]={
        {"Brush",           ACT_Brush},
        {"AmbientSound",    ACT_AmbientSound},
        {"PlayerStart",     ACT_PlayerStart},
        {"Light",           ACT_Light},
        {"Spotlight",       ACT_Spotlight},
        {"Sparks",          ACT_Sparks},

        {NULL}
};

unsigned int GetActorIdentification(const char *name) {
    for(unsigned int i = 0; i < plArrayElements(actor_definitions); ++i) {
        if(actor_definitions[i].name == NULL) {
            break;
        }

        if(pl_strncasecmp(name, actor_definitions[i].name, sizeof(name)) == 0) {
            return actor_definitions[i].id;
        }
    }

    return ACT_Unknown;
}

typedef struct Actor {
    char class[64];
    unsigned int class_i;

    UVector position;
} Actor;

/****************************/

typedef struct Polygon { /* i 'ssa face >:I */
    char item[32];
    char texture[64];
    char group[16];

    int link;

    UVector vertices[4];
    UVector u;
    UVector v;

    UVector origin;
} Polygon;

typedef struct Brush { /* i 'ssa primitive >:I */
    char name[32];

    Polygon poly_list[MAX_BRUSH_FACES];
    Polygon *cur_poly;
    unsigned int num_poly;

    UVector location;
    UVector rotation;
    UVector pre_pivot;
    UVector post_pivot;

    enum {
        CSG_Active,
        CSG_Add,
        CSG_Subtract,
        CSG_Intersect,
        CSG_Deintersect,
    } csg;

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

/* yeah, yeah... I know... shut-up. */
#define SkipLine()      while(*t3d.cur_pos != '\n' && *t3d.cur_pos != '\r') { t3d.cur_pos++; } t3d.cur_line++;
#define SkipSpaces()    while(*t3d.cur_pos == ' ') { t3d.cur_pos++; }
#define ParseBlock()    while(*t3d.cur_pos != '\0')
#define ParseLine()     while(*t3d.cur_pos != '\0' && *t3d.cur_pos != '\n' && *t3d.cur_pos != '\r')

void ParseString(char *out) {
    unsigned int i = 0;
    ParseBlock() {
        if(*t3d.cur_pos == ' ' || *t3d.cur_pos == '\n' || *t3d.cur_pos == '\r') {
            break;
        }

        out[i++] = *t3d.cur_pos++;
    }
    out[i] = '\0';
}

void ParseNext(void) {
    ParseBlock() {
        if (*t3d.cur_pos == '\n' || *t3d.cur_pos == '\r' || *t3d.cur_pos == '\t') {
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
    char n[4];
    ParseString(n);
    return (atoi(n));
}

float ParseVectorCoordinate(void) {
    float f = strtof(t3d.cur_pos, (char**)(&t3d.cur_pos));
    if (*t3d.cur_pos == ',') t3d.cur_pos++;

#ifdef DEBUG_PARSER /* debug */
    printf("read coord %f\n", f);
#endif

    return f;
}

UVector ParseVector(void) {
    if(*t3d.cur_pos == '(') t3d.cur_pos++;

    UVector vector = {
        ParseVectorCoordinate(),
        ParseVectorCoordinate(),
        ParseVectorCoordinate()
    };

    SkipLine();

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

    size_t len = strlen(prop);
    if(pl_strncasecmp(t3d.cur_pos, prop, len) == 0) {
        t3d.cur_pos += len;
        return true;
    }

    return false;
}

bool ReadVectorField(const char *prop, UVector *vector) {
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

    char prop[16];
    snprintf(prop, sizeof(prop), "%s=", parm);

    size_t len = strlen(prop);
    if(pl_strncasecmp(t3d.cur_pos, prop, len) == 0) {
        t3d.cur_pos += len;
        return true;
    }

    return false;
}

bool ReadPropertyString(const char *parm, char *out) {
    if(ReadProperty(parm)) {
        ParseString(out);
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
        ParseString(chunk_name);
        printf("unhandled chunk \"%s\"!\n", chunk_name);
        exit(EXIT_FAILURE);
    }
    t3d.cur_chunk--;
}

#define print_heading(a)    for(unsigned int i = 0; i < t3d.cur_chunk; ++i) printf(" "); printf("parsing " a "\n");

void ReadPolygon(void) {
    print_heading("Polygon");

    ParseLine() {
        if(ReadPropertyString("Item", t3d.cur_brush->cur_poly->item)) {
            continue;
        }

        if(ReadPropertyInteger("Link", &t3d.cur_brush->cur_poly->link)) {
            continue;
        }

        if(ReadPropertyString("Texture", t3d.cur_brush->cur_poly->texture)) {
            continue;
        }

        if(ReadPropertyString("Group", t3d.cur_brush->cur_poly->group)) {
            continue;
        }

        SkipProperty();
    }

    if(plIsEmptyString(t3d.cur_brush->cur_poly->texture)) {
        strncpy(t3d.cur_brush->cur_poly->texture, "none", sizeof(t3d.cur_brush->cur_poly->texture));
    }

    unsigned int cur_vertex = 0;
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

        if(ReadVectorField("Vertex", &t3d.cur_brush->cur_poly->vertices[cur_vertex++])) {
            continue;
        }

        SkipLine();
    }

    t3d.cur_brush->cur_poly++;
}

void ReadPolyList(void) {
    print_heading("PolyList");

    t3d.cur_brush->cur_poly = &t3d.cur_brush->poly_list[0];

    ParseLine() {
        if(ReadPropertyInteger("Num", (int *) &t3d.cur_brush->num_poly)) {
            continue;
        }

        SkipProperty();
    }

    if(t3d.cur_brush->num_poly == 0) {
        printf("invalid number of polygons in polylist!\n");
        exit(EXIT_FAILURE);
    }

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
}

void ReadMap(void) {
    print_heading("Map");

    /* header */
    ParseLine() {
        if(ReadPropertyString("Name", t3d.map.name)) {
            continue;
        }

        if(ReadPropertyInteger("Brushes", (int*) &t3d.num_brushes)) {
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

    ParseLine() {
        if(ReadPropertyString("Class", t3d.cur_actor->class)) {
            t3d.cur_actor->class_i = GetActorIdentification(t3d.cur_actor->class);
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

        SkipLine();
    }

    t3d.num_actors++;
    t3d.cur_actor++;
}

void ReadBrush(void) {
    t3d.chunks[t3d.cur_chunk].context = CTX_BRUSH;

    print_heading("Brush");

    ParseLine() {
        if(ReadPropertyString("Name", t3d.map.name)) {
            continue;
        }

        SkipProperty();
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

    FILE *fp = fopen(path, "r");
    if(fp == NULL) {
        printf("failed to read \"%s\", aborting!\n", path);
        exit(EXIT_FAILURE);
    }

    char *buf;
    size_t length = plGetFileSize(path);
    buf = malloc(length);
    fread(buf, 1, length, fp);
    buf[length] = '\0';
    fclose(fp);

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

const char *GetEntityForActor(Actor *actor) {
    if(startup_actors || actor->class_i == ACT_Unknown) {
        if(actor->class[0] == '\0' || actor->class[0] == ' ') {
            printf("warning: invalid actor name, possibly failed to parse?\n");
            return "unknown";
        }
        return &actor->class[0];
    }

    switch(actor->class_i) {
        default:break;

        case ACT_Sparks: {
            if(startup_format == MAP_FORMAT_SRC) {
                return "env_spark";
            }
        } break;

        case ACT_Light: {
            return "light";
        }

        case ACT_Spotlight: {
            if(startup_format == MAP_FORMAT_SRC) {
                return "light_spot";
            }

            return "light";
        }

        case ACT_PlayerStart: {
            return "info_player_start";
        }
    }

    return &actor->class[0];
}

void WriteMap(const char *path) {
    FILE *fp = fopen(path, "w");
    if(fp == NULL) {
        printf("failed to open \"%s\", aborting!\n", path);
        exit(EXIT_FAILURE);
    }

#define WriteField(a, b)  fprintf(fp, "\"%s\" \"%s\"\n", (a), (b))
#define WriteVector(a, b) fprintf(fp, "\"%s\" \"%d %d %d\"\n", (a), (int)(b).x, (int)(b).y, (int)(b).z);

    /* write out the world spawn */

    fprintf(fp, "{\n");
    switch(startup_format) {
        default:
        case MAP_FORMAT_IDT2: {
            WriteField(" classname", "worldspawn");
            WriteField(" wad", "/gfx/base.wad");
            WriteField(" worldtype", "0");
        } break;
    }

    printf("writing %d brushes...\n", t3d.num_brushes);
    for(unsigned int i = 0; i < t3d.num_brushes; ++i) {
        fprintf(fp, " {\n");

#ifdef DEBUG_PARSER
        printf("brush %d\n", i);
        printf(" name: %s\n", t3d.brushes[i].name);
        printf(" csg:  %d\n", t3d.brushes[i].csg);
#endif

        for(unsigned int j = 0; j < t3d.brushes[i].num_poly; ++j) {
            Polygon *cur_face = &t3d.brushes[i].poly_list[j];

            fprintf(fp, "  ( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s 0 0 0 0 0\n",
                    (int) cur_face->vertices[0].x, (int) cur_face->vertices[0].y, (int) cur_face->vertices[0].z,
                    (int) cur_face->vertices[1].x, (int) cur_face->vertices[1].y, (int) cur_face->vertices[1].z,
                    (int) cur_face->vertices[2].x, (int) cur_face->vertices[2].y, (int) cur_face->vertices[2].z,
                    cur_face->texture
            );

#ifdef DEBUG_PARSER
            printf(" poly %d\n", j);
            printf("  texture: %s\n", cur_face->texture);
            printf("  group:   %s\n", cur_face->group);
            printf("  item:    %s\n", cur_face->item);
#endif
        }

        fprintf(fp, " }\n");
    }

    fprintf(fp, "}\n");

    for(unsigned int i = 0; i < t3d.num_actors; ++i) {
        fprintf(fp, "{\n");

        WriteField(" classname", GetEntityForActor(&t3d.actors[i]));
        WriteVector(" origin", t3d.actors[i].position);

        if(pl_strncasecmp(t3d.actors[i].class, "light", 5) == 0) {
            WriteField(" light", "255");
        }

        fprintf(fp, "}\n");
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
            {
                "-test",
                &startup_test, NULL,
                "if enabled, this will simply read the input and won't export anything"
            },

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

            {
                "-actors",
                &startup_actors, NULL,
                "retain original actor names for entities"
            },

            {NULL, NULL}
    };

    printf("t3d2map v" VERSION "\nDeveloped by Mark \"hogsy\" Sowden <markelswo@gmail.com>\n\n");
    if(argc < 2) {
        printf("\nusage:\n t3d2map <in> [out]\n");
        for(unsigned int i = 0; i < plArrayElements(launch_arguments); ++i) {
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
        plStripExtension(ext, plGetFileName(in_path));
        snprintf(out_path, PL_SYSTEM_MAX_PATH, "./%s.map", ext);
    }

    /* lil slower, but simpler to implement new commands... */

    for(unsigned int i = 2; i < argc; i++) {
        if(argv[i][0] == '-') {
            for(unsigned int j = 0; j < plArrayElements(launch_arguments); ++j) {
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

    printf("done!\n");

    return EXIT_SUCCESS;
}