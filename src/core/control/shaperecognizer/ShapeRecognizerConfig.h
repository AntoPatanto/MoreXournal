/*
 * Xournal++
 *
 * Part of the Xournal shape recognizer
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once
#include <config-debug.h>
#include <glib.h>

#define MAX_POLYGON_SIDES 4

#define LINE_POINT_DIST2_THRESHOLD 15  // threshold of maximum distance from point to line


#ifdef DEBUG_RECOGNIZER
#define RDEBUG(msg, ...) g_message("ShapeReco::" msg, ##__VA_ARGS__)
#else
#define RDEBUG(msg, ...)
#endif
