/*
 * texture.c - Code to use modern OpenGL to render an image as a texture.
 *
 * All of the OpenGL interaction happens here, except for a single call to
 * glViewport() in window.c.
 *
 * Yes, it takes this much code to put a single image on the screen,
 * if I want to use up-to-date graphics coding standards.
 */
#define	GL3_PROTOTYPES
#include "common.h"
#include "gfxhdr.h"

#include "debug.h"
#include "texture.h"

/* ------------------------------------------------------------------ */

static struct {
	GLuint		program_id;	/* rendering program ID */

	GLuint		vertex_array;	/* vertex array object */

	GLuint		vertex_buffer;	/* Coordinate buffers for rendering */
	GLuint		texcoord_buffer;

	GLint		texcoord_loc;	/* program locations of data */
	GLint		vertex_loc;
	GLint		texunit_loc;

	GLuint		texture_id;	/* OpenGL texture ID */
} Texture;

/*
 * ------------------------------------------------------------------
 * GLSL source for vertex and fragment shaders.
 */

static const char *Vertex_shader_source =
"\n#version 330 core\n"

// Vertex position attribute
"layout(location = 0) in vec2 Vertex_pos;"

// Texture coordinate attribute
"layout(location = 1) in vec2 Texture_pos;"

// Output data; will be interpolated for each fragment (pixel).
"out vec2 Texture_coord;"

"void main()"
"{"
	// Process the vertex
"	gl_Position = vec4(Vertex_pos, 1, 1);"

	// Process the texture coordinate
"	Texture_coord = Texture_pos;"
"}"
;

static const char *Fragment_shader_source =
"\n#version 330 core\n"

// Interpolated values from the vertex shaders
"in vec2 Texture_coord;"

// Final color for the fragment
"out vec3 Frag_color;"

// Texture unit - constant for the entire image
"uniform sampler2D Texture_unit;"

"void main()"
"{"
"	Frag_color = texture(Texture_unit, Texture_coord).rgb;"
"}"
;

/* ------------------------------------------------------------------ */

/*
 * Compile a GLSL shader.
 */
static GLuint
compile_shader(GLuint shader_type, const char *shader_source)
{
	GLuint	shader_id;
	GLint	result;
	GLint	log_length;

        shader_id = glCreateShader(shader_type);
	glShaderSource(shader_id, 1, &shader_source, NULL);
	glCompileShader(shader_id);

	glGetShaderiv(shader_id, GL_COMPILE_STATUS, &result);
	glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0) {
		char *error_message = malloc(log_length + 2);
		glGetShaderInfoLog(shader_id, log_length, NULL,
		    error_message);
		error_message[log_length] = '\n';
		error_message[log_length + 1] = '\0';
		die(error_message);
	}

	return (shader_id);
}

/*
 * Compile the vertex and fragment shaders, and link them together.
 */
static GLuint
link_shaders(void)
{
        GLuint	vertex_shader_id;
        GLuint	fragment_shader_id;
	GLuint	program_id;
	GLint	result;
	GLint	log_length;

	vertex_shader_id =
	    compile_shader(GL_VERTEX_SHADER, Vertex_shader_source);
	fragment_shader_id =
	    compile_shader(GL_FRAGMENT_SHADER, Fragment_shader_source);

	program_id = glCreateProgram();
	glAttachShader(program_id, vertex_shader_id);
	glAttachShader(program_id, fragment_shader_id);
	glLinkProgram(program_id);

	glGetProgramiv(program_id, GL_LINK_STATUS, &result);
	glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0) {
		char *error_message = malloc(log_length + 2);
		glGetProgramInfoLog(program_id, log_length, NULL,
		    error_message);
		error_message[log_length] = '\n';
		error_message[log_length + 1] = '\0';
		die(error_message);
	}

	glDetachShader(program_id, vertex_shader_id);
	glDetachShader(program_id, fragment_shader_id);

	glDeleteShader(vertex_shader_id);
	glDeleteShader(fragment_shader_id);

        return (program_id);
}

/* ------------------------------------------------------------------ */

/*
 * This is called directly by window.c, rather than via the module API.
 */
int
texture_init(float width_fraction, float height_fraction)
{
	/*
	 * Data for mapping between the texture and the screen window.
	 * It is represented as two triangles, rather than one rectangle,
	 * because the hardware thinks of everything in terms of triangles.
	 */
	const float	wf = 1.0f; // was "width_fraction"
	const float	hf = 1.0f; // was "height_fraction"
	const GLfloat	vertices[6][2] = {
		{ -wf,  hf }, { -wf, -hf }, {  wf, -hf },
		{ -wf,  hf }, {  wf, -hf }, {  wf,  hf }
	};
	const GLfloat	texcoords[6][2] = {
		{ 0, 0 }, { 0, 1 }, { 1, 1 },
		{ 0, 0 }, { 1, 1 }, { 1, 0 }
	};

	/* Compile the GLSL programs. */
	Texture.program_id = link_shaders();

	/* Generate a vertex array object. */
	glGenVertexArrays(1, &Texture.vertex_array);
	glBindVertexArray(Texture.vertex_array);

	/* Generate the OpenGL buffers for the texture mapping metadata. */
	glGenBuffers(1, &Texture.vertex_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, Texture.vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof (vertices), vertices,
	    GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &Texture.texcoord_buffer);
	glBindBuffer(GL_ARRAY_BUFFER, Texture.texcoord_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof (texcoords), texcoords,
	    GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	/* Generate the OpenGL texture we're using to render the data. */
	glGenTextures(1, &Texture.texture_id);
	glBindTexture(GL_TEXTURE_2D, Texture.texture_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
	    Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	/*
	 * Since we're only displaying one thing (our data, as a texture),
	 * bind and enable all of our OpenGL resources now.
	 */
	Texture.vertex_loc = glGetAttribLocation(Texture.program_id,
	    "Vertex_pos");
	Texture.texcoord_loc = glGetAttribLocation(Texture.program_id,
	    "Texture_pos");
	Texture.texunit_loc = glGetAttribLocation(Texture.program_id,
	    "Texture_unit");

	/* Use our compiled program. */
	glUseProgram(Texture.program_id);

	/* Use our texture buffer. */
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, Texture.texture_id);

	/* Point the vertex shader at our vertex and texture coordinate data. */
	glEnableVertexAttribArray(Texture.vertex_loc);
	glBindBuffer(GL_ARRAY_BUFFER, Texture.vertex_buffer);
	glVertexAttribPointer(Texture.vertex_loc,
	    2, GL_FLOAT, GL_FALSE, 0, NULL);

	glEnableVertexAttribArray(Texture.texcoord_loc);
	glBindBuffer(GL_ARRAY_BUFFER, Texture.texcoord_buffer);
	glVertexAttribPointer(Texture.texcoord_loc,
	    2, GL_FLOAT, GL_FALSE, 0, NULL);

	/* Tell the fragment shader how to find our texture. */
	glUniform1i(Texture.texunit_loc, Texture.texture_id);

	glFinish();

	/*
	 * Finally, make an OpenCL handle for this texture, and return it.
	 * This is how the rest of the program accesses it.
	 */
	return (Texture.texture_id);
}

void
texture_render(void)
{
	/*
	 * Everything is set up; we just need to draw the triangles.
	 * We have 2 of them, with 3 vertices per triangle.
	 */
	glDrawArrays(GL_TRIANGLES, 0, 2 * 3);
}

void
texture_fini(void)
{
	/* Disable the objects. */
	glDisableVertexAttribArray(Texture.vertex_loc);
	glDisableVertexAttribArray(Texture.texcoord_loc);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);

	/* Delete the objects. */
	glDeleteTextures(1, &Texture.texture_id);
	glDeleteBuffers(1, &Texture.vertex_buffer);
	glDeleteBuffers(1, &Texture.texcoord_buffer);
	glDeleteVertexArrays(1, &Texture.vertex_array);
	glDeleteProgram(Texture.program_id);
}
