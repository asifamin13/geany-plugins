/*
 *      bracketcolors.cc
 *
 *      Copyright 2023 Asif Amin <asifamin@utexas.edu>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */


/* --------------------------------- INCLUDES ------------------------------- */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#include <string>
#include <array>
#include <set>
#include <map>
#include <memory>

#include <glib.h>

#include <geanyplugin.h>
#include "sciwrappers.h"

#include "BracketMap.h"

#define BC_NUM_COLORS 3
#define BC_NO_ARG 0
#define BC_STOP_ACTION TRUE
#define BC_CONTINUE_ACTION FALSE

#define SSM(s, m, w, l) scintilla_send_message(s, m, w, l)


/* --------------------------------- CONSTANTS ------------------------------ */

    typedef std::array<std::string, BC_NUM_COLORS> BracketColorArray;

    /*
     * These were copied from VS Code
     * TODO: Make this user configurable, get from theme?
     */

    static const BracketColorArray sDarkBackgroundColors = {
        "#FF00FF", "#FFFF00", "#00FFFF"
    };

    static const BracketColorArray sLightBackgroundColors = {
        "#008000", "#000080", "#800000"
    };

    static const gchar *sPluginName = "bracketcolors";

    // start index of indicators our plugin will use
    static const guint sIndicatorIndex = INDICATOR_IME - BC_NUM_COLORS;

/* ----------------------------------- TYPES -------------------------------- */

    enum BracketType {
        PAREN = 0,
        BRACE,
        BRACKET,
        ANGLE,
        COUNT
    };

    struct BracketColorsData {

        /*
         * Associated with every document
         */

        GeanyDocument *doc;

        guint32 backgroundColor;
        BracketColorArray bracketColors;

        gboolean init;

        guint computeTimeoutID, computeInterval;
        guint drawTimeoutID;

        gboolean updateUI;
        std::set<BracketMap::Index> recomputeIndicies, redrawIndicies;

        gboolean bracketColorsEnable[BracketType::COUNT];
        BracketMap bracketMaps[BracketType::COUNT];

        BracketColorsData() :
            doc(NULL),
            bracketColors(sLightBackgroundColors),
            init(FALSE),
            computeTimeoutID(0),
            computeInterval(500),
            drawTimeoutID(0),
            updateUI(FALSE)
        {
            for (guint i = 0; i < BracketType::COUNT; i++) {
                bracketColorsEnable[i] = TRUE;
            }

            /*
             * color matching angle brackets seems to cause
             * more confusion than its worth
             */

            bracketColorsEnable[BracketType::ANGLE] = FALSE;
        }

        ~BracketColorsData() {}

        void RemoveFromQueues(BracketMap::Index index);
        void StartTimers();
        void StopTimers();
    };

    struct BracketColorsPluginSetting {

        /*
         * Settings configuration base class
         */

        std::string mGroup, mKey;
        gpointer mValue;

        BracketColorsPluginSetting(
            std::string group,
            std::string key,
            gpointer value
        ) : mGroup(group), mKey(key), mValue(value)
        {
            // nothing to do
        }

        virtual void read(GKeyFile *kf)=0;
        virtual void write(GKeyFile *kf)=0;
    };

    struct BooleanSetting : public BracketColorsPluginSetting {

        BooleanSetting(
            std::string group,
            std::string key,
            gpointer value
        ) : BracketColorsPluginSetting(group, key, value)
        {
            // nothing to do
        }

        void read(GKeyFile *kf) {
            gboolean *aBool = static_cast<gboolean *>(mValue);
            *aBool = utils_get_setting_boolean(kf, mGroup.c_str(), mKey.c_str(), *aBool);
        }

        void write(GKeyFile *kf) {
            const gboolean *aBool = static_cast<gboolean *>(mValue);
            g_key_file_set_boolean(kf, mGroup.c_str(), mKey.c_str(), *aBool);
        }
    };

/* ---------------------------------- GLOBALS ------------------------------- */

    // Whether to use the auto defaults
    gboolean gUseDefaults = TRUE;

    // Custom user defined colors
    BracketColorArray gCustomColors;

    std::array<std::shared_ptr<BracketColorsPluginSetting>, 1> gPluginSettings = {
        std::make_shared<BooleanSetting>("general", "defaults", &gUseDefaults)
    };

/* ---------------------------------- EXTERNS ------------------------------- */

    GeanyPlugin *geany_plugin;
    GeanyData *geany_data;

/* --------------------------------- PROTOTYPES ----------------------------- */

    static gboolean recompute_brackets_timeout(gpointer user_data);
    static gboolean render_brackets_timeout(gpointer user_data);

/* ------------------------------ IMPLEMENTATION ---------------------------- */


// -----------------------------------------------------------------------------
    void BracketColorsData::StartTimers()

/*

----------------------------------------------------------------------------- */
{
    if (computeTimeoutID == 0) {
        computeTimeoutID = g_timeout_add_full(
            G_PRIORITY_LOW,
            20,
            recompute_brackets_timeout,
            this,
            NULL
        );
    }

    if (drawTimeoutID == 0) {
        drawTimeoutID = g_timeout_add_full(
            G_PRIORITY_LOW,
            100,
            render_brackets_timeout,
            this,
            NULL
        );
    }
}



// -----------------------------------------------------------------------------
    void BracketColorsData::StopTimers()

/*

----------------------------------------------------------------------------- */
{
    if (computeTimeoutID > 0) {
        g_source_remove(computeTimeoutID);
        computeTimeoutID = 0;
    }

    if (drawTimeoutID > 0) {
        g_source_remove(drawTimeoutID);
        drawTimeoutID = 0;
    }
}



// -----------------------------------------------------------------------------
    void BracketColorsData::RemoveFromQueues(BracketMap::Index index)

/*

----------------------------------------------------------------------------- */
{
    {
        auto it = recomputeIndicies.find(index);
        if (it != recomputeIndicies.end()) {
            recomputeIndicies.erase(it);
        }
    }
    {
        auto it = redrawIndicies.find(index);
        if (it != redrawIndicies.end()) {
            redrawIndicies.erase(it);
        }
    }
}


// -----------------------------------------------------------------------------
    static gboolean utils_is_dark(guint32 color)

/*

----------------------------------------------------------------------------- */
{
    guint8 b = color >> 16;
    guint8 g = color >> 8;
    guint8 r = color;

    // https://stackoverflow.com/questions/596216/formula-to-determine-perceived-brightness-of-rgb-color
    guint8 y  = ((r << 1) + r + (g << 2) + b) >> 3;

    if (y < 125) {
        return TRUE;
    }

    return FALSE;
}



// -----------------------------------------------------------------------------
    static gboolean utils_parse_color(
        const gchar *spec,
        GdkColor *color
    )
/*

----------------------------------------------------------------------------- */
{
    gchar buf[64] = {0};

    g_return_val_if_fail(spec != NULL, -1);

    if (spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X'))
    {
        /* convert to # format for GDK to understand it */
        buf[0] = '#';
        strncpy(buf + 1, spec + 2, sizeof(buf) - 2);
        spec = buf;
    }

    return gdk_color_parse(spec, color);
}



// -----------------------------------------------------------------------------
    static gint utils_color_to_bgr(const GdkColor *c)
/*

----------------------------------------------------------------------------- */
{
    g_return_val_if_fail(c != NULL, -1);
    return (c->red / 256) | ((c->green / 256) << 8) | ((c->blue / 256) << 16);
}



// -----------------------------------------------------------------------------
    static gint utils_parse_color_to_bgr(const gchar *spec)
/*

----------------------------------------------------------------------------- */
{
    GdkColor color;
    if (utils_parse_color(spec, &color)) {
        return utils_color_to_bgr(&color);
    }
    else {
        return -1;
    }
}



// -----------------------------------------------------------------------------
    static void assign_indicator_colors(
        BracketColorsData *data
    )
/*

----------------------------------------------------------------------------- */
{
    ScintillaObject *sci = data->doc->editor->sci;

    for (guint i = 0; i < data->bracketColors.size(); i++) {
        guint index = sIndicatorIndex + i;
        std::string spec = data->bracketColors.at(i);
        gint color = utils_parse_color_to_bgr(spec.c_str());
        SSM(sci, SCI_INDICSETSTYLE, index, INDIC_TEXTFORE);
        SSM(sci, SCI_INDICSETFORE, index, color);
    }
}



// -----------------------------------------------------------------------------
    static gboolean has_document(void)
/*
    sanity check
----------------------------------------------------------------------------- */
{
    GtkNotebook *notebook = GTK_NOTEBOOK(geany_data->main_widgets->notebook);
    gint currPage = gtk_notebook_get_current_page(notebook);
    return currPage >= 0 ? TRUE : FALSE;
}



// -----------------------------------------------------------------------------
    static gboolean is_curr_document(
        BracketColorsData *data
    )
/*
    check if this document is currently opened
----------------------------------------------------------------------------- */
{
    GtkNotebook *notebook = GTK_NOTEBOOK(geany_data->main_widgets->notebook);
    gint currPage = gtk_notebook_get_current_page(notebook);
    GeanyDocument *currDoc = document_get_from_page(currPage);

    if (currDoc != NULL and currDoc == data->doc) {
        return TRUE;
    }

    return FALSE;
}



// -----------------------------------------------------------------------------
    static void bracket_colors_data_free(gpointer data)
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *bcd = reinterpret_cast<BracketColorsData *>(data);
    delete bcd;
}



// -----------------------------------------------------------------------------
    static BracketColorsData* bracket_colors_data_new(GeanyDocument *doc)
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *newBCD = new BracketColorsData();

    plugin_set_document_data_full(
        geany_plugin,
        doc,
        sPluginName,
        newBCD,
        bracket_colors_data_free
    );

    return newBCD;
}



// -----------------------------------------------------------------------------
    static gboolean is_bracket_type(
        gchar ch,
        BracketType type
    )
/*
    check if char is bracket type
----------------------------------------------------------------------------- */
{
    static const std::set<gchar> sAllBrackets {
        '(', ')',
        '[', ']',
        '{', '}',
        '<', '>',
    };

    switch (type) {
        case (BracketType::PAREN): {
            if (ch == '(' or ch == ')') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACE): {
            if (ch == '[' or ch == ']') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACKET): {
            if (ch == '{' or ch == '}') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::ANGLE): {
            if (ch == '<' or ch == '>') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::COUNT): {
            return sAllBrackets.find(ch) != sAllBrackets.end() ? TRUE : FALSE;
        }
        default:
            return FALSE;
    }

}



// -----------------------------------------------------------------------------
    static gboolean is_open_bracket(
        gchar ch,
        BracketType type
    )
/*
    check if char is open bracket type
----------------------------------------------------------------------------- */
{
    static const std::set<gchar> sAllOpenBrackets {
        '(',
        '[',
        '{',
        '<',
    };

    switch (type) {
        case (BracketType::PAREN): {
            if (ch == '(') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACE): {
            if (ch == '[') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACKET): {
            if (ch == '{') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::ANGLE): {
            if (ch == '<') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::COUNT): {
            return sAllOpenBrackets.find(ch) != sAllOpenBrackets.end() ? TRUE : FALSE;
        }
        default:
            return FALSE;
    }

}



// -----------------------------------------------------------------------------
    static gint compute_bracket_at(
        ScintillaObject *sci,
        BracketMap &bracketMap,
        gint position,
        bool updateInvalidMapping = true
    )
/*
    compute bracket at position
----------------------------------------------------------------------------- */
{
    gint matchedBrace = SSM(sci, SCI_BRACEMATCH, position, BC_NO_ARG);
    gint braceIdentity = position;

    if (matchedBrace != -1) {

        gint length = matchedBrace - position;

        if (length > 0) {
            // matched from start brace
            bracketMap.Update(position, length);
        }
        else {
            // matched from end brace
            length = -length;
            braceIdentity = position - length;
            bracketMap.Update(braceIdentity, length);
        }
    }
    else {
        // invalid mapping

        if (is_open_bracket(sci_get_char_at(sci, position), BracketType::COUNT)) {
            if (updateInvalidMapping) {
                bracketMap.Update(position, BracketMap::UNDEFINED);
            }
        }
        else {
            // unknown start brace
            braceIdentity = -1;
        }
    }

    return braceIdentity;
}



// -----------------------------------------------------------------------------
    static gboolean is_ignore_style(
        BracketColorsData *data,
        gint position
    )
/*
    check if position is part of non source section
----------------------------------------------------------------------------- */
{
    ScintillaObject *sci = data->doc->editor->sci;

    gint style = SSM(sci, SCI_GETSTYLEAT, position, BC_NO_ARG);
    gint lexer = sci_get_lexer(sci);

    return not highlighting_is_code_style(lexer, style);
}



// -----------------------------------------------------------------------------
    static void find_all_brackets(
        BracketColorsData &data
    )
/*
    brute force search for brackets
----------------------------------------------------------------------------- */
{
    ScintillaObject *sci = data.doc->editor->sci;

    gint length = sci_get_length(sci);
    for (gint i = 0; i < length; i++) {
        gchar ch = sci_get_char_at(sci, i);
        if (is_bracket_type(ch, BracketType::COUNT)) {
            for (gint bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                if (data.bracketColorsEnable[bracketType] == TRUE) {
                    if (is_bracket_type(ch, static_cast<BracketType>(bracketType))) {
                        data.recomputeIndicies.insert(i);
                        data.updateUI = TRUE;
                        break;
                    }
                }
            }
        }
    }
}



// -----------------------------------------------------------------------------
    static void remove_bc_indicators(
        ScintillaObject *sci
    )
/*
    remove indicators associated with this plugin
----------------------------------------------------------------------------- */
{
    gint length = sci_get_length(sci);
    for (gint i = 0; i < BC_NUM_COLORS; i++) {
        SSM(sci, SCI_SETINDICATORCURRENT, sIndicatorIndex + i, BC_NO_ARG);
        SSM(sci, SCI_INDICATORCLEARRANGE, 0, length);
    }
}



// -----------------------------------------------------------------------------
    static void set_bc_indicators_at(
        ScintillaObject *sci,
        const BracketColorsData &data,
        gint index
    )
/*
    assign indicator at position, check if already correct
----------------------------------------------------------------------------- */
{
    for (gint i = 0; i < BracketType::COUNT; i++) {

        const BracketMap &bracketMap = data.bracketMaps[i];

        auto it = bracketMap.mBracketMap.find(index);
        if (it == bracketMap.mBracketMap.end()) {
            continue;
        }

        auto bracket = it->second;

        if (BracketMap::GetLength(bracket) != BracketMap::UNDEFINED) {

            std::array<gint, 2> positions {
                { index, index + BracketMap::GetLength(bracket) }
            };

            for (auto position : positions) {

                unsigned correctIndicatorIndex = sIndicatorIndex + \
                    ((BracketMap::GetOrder(bracket) + i) % BC_NUM_COLORS);

                gint curr = SSM(sci, SCI_INDICATORVALUEAT, correctIndicatorIndex, position);
                if (not curr) {
                    SSM(
                        sci,
                        SCI_SETINDICATORCURRENT,
                        correctIndicatorIndex,
                        BC_NO_ARG
                    );
                    SSM(sci, SCI_INDICATORFILLRANGE, position, 1);
                }

                // make sure there arent any other indicators at position
                for (
                    guint indicatorIndex = sIndicatorIndex;
                    indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
                    indicatorIndex++
                )
                {
                    if (indicatorIndex == correctIndicatorIndex) {
                        continue;
                    }

                    gint hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, position);
                    if (hasIndicator) {
                        SSM(
                            sci,
                            SCI_SETINDICATORCURRENT,
                            indicatorIndex,
                            BC_NO_ARG
                        );
                        SSM(sci, SCI_INDICATORCLEARRANGE, position, 1);
                    }
                }
            }
        }
    }
}




// -----------------------------------------------------------------------------
    static void clear_bc_indicators(
        ScintillaObject *sci,
        gint position, gint length
    )
/*
    clear bracket indicators in range
----------------------------------------------------------------------------- */
{
    for (gint i = position; i < position + length; i++) {
        for (
            guint indicatorIndex = sIndicatorIndex;
            indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
            indicatorIndex++
        )
        {
            gint hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, i);
            if (hasIndicator) {
                SSM(
                    sci,
                    SCI_SETINDICATORCURRENT,
                    indicatorIndex,
                    BC_NO_ARG
                );
                SSM(sci, SCI_INDICATORCLEARRANGE, i, 1);
            }
        }
    }
}



// -----------------------------------------------------------------------------
    static gboolean move_brackets(
        ScintillaObject *sci,
        BracketColorsData &bracketColorsData,
        gint position, gint length,
        BracketType type
    )
/*
    handle when text is added
----------------------------------------------------------------------------- */
{
    if (bracketColorsData.bracketColorsEnable[type] == FALSE) {
        return FALSE;
    }

    BracketMap &bracketMap = bracketColorsData.bracketMaps[type];

    std::set<BracketMap::Index> indiciesToAdjust, indiciesToRecompute;

    /*
     * Look through existing bracket map and check if addition of characters
     * will require adjustment
     */

    for (const auto &it : bracketMap.mBracketMap) {
        const auto &bracket = it.second;
        gint endPos = it.first + BracketMap::GetLength(bracket);
        if (it.first >= position) {
            indiciesToAdjust.insert(it.first);
        }
        else if (
            endPos >= position or
            BracketMap::GetLength(bracket) == BracketMap::UNDEFINED
        ) {
            indiciesToRecompute.insert(it.first);
        }
    }

    gboolean madeChange = FALSE;

    // Check if the new characters that are added were brackets
    for (gint i = position; i < position + length; i++) {
        gchar newChar = sci_get_char_at(sci, i);
        if (is_bracket_type(newChar, type)) {
            madeChange = TRUE;
            bracketColorsData.recomputeIndicies.insert(i);
        }
    }

    if (not indiciesToAdjust.size() and not indiciesToRecompute.size()) {
        return madeChange;
    }

    bracketColorsData.recomputeIndicies.insert(
        indiciesToRecompute.begin(), indiciesToRecompute.end()
    );

    for (const auto &it : indiciesToAdjust) {
        bracketMap.mBracketMap.insert(
            std::make_pair(
                it + length,
                bracketMap.mBracketMap.at(it)
            )
        );

        bracketMap.mBracketMap.erase(it);
        bracketColorsData.RemoveFromQueues(it);
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static gboolean remove_brackets(
        ScintillaObject *sci,
        BracketColorsData &bracketColorsData,
        gint position, gint length,
        BracketType type
    )
/*
    handle when text is removed
----------------------------------------------------------------------------- */
{
    if (bracketColorsData.bracketColorsEnable[type] == FALSE) {
        return FALSE;
    }

    BracketMap &bracketMap = bracketColorsData.bracketMaps[type];

    std::set<BracketMap::Index> indiciesToRemove, indiciesToRecompute;

    for (const auto &it : bracketMap.mBracketMap) {
        const auto &bracket = it.second;
        gint endPos = it.first + BracketMap::GetLength(bracket);
        // start bracket was deleted
        if ( (it.first >= position) and (it.first < position + length) ) {
            indiciesToRemove.insert(it.first);
        }
        // end bracket removed or space removed
        else if (it.first >= position or endPos >= position) {
            indiciesToRecompute.insert(it.first);
        }
    }

    if (
        not indiciesToRemove.size() and
        not indiciesToRecompute.size()
    ) {
        return FALSE;
    }

    for (const auto &it : indiciesToRemove) {
        bracketMap.mBracketMap.erase(it);
        bracketColorsData.RemoveFromQueues(it);
    }

    for (const auto &it : indiciesToRecompute) {
        // first bracket was moved backwards
        if (it >= position) {
            bracketMap.mBracketMap.insert(
                std::make_pair(
                    it - length,
                    bracketMap.mBracketMap.at(it)
                )
            );
            bracketMap.mBracketMap.erase(it);
            bracketColorsData.RemoveFromQueues(it);
        }
        // last bracket was moved
        else {
            bracketColorsData.recomputeIndicies.insert(it);
        }
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static void render_document(
        ScintillaObject *sci,
        BracketColorsData *data
    )
/*

----------------------------------------------------------------------------- */
{
    if (data->updateUI) {

        for (
            auto position = data->redrawIndicies.begin();
            position != data->redrawIndicies.end();
            position++
        )
        {
            // if this bracket has been reinserted into the work queue, ignore
            if (data->recomputeIndicies.find(*position) == data->recomputeIndicies.end()) {
                set_bc_indicators_at(sci, *data, *position);
            }
        }

        data->redrawIndicies.clear();
        data->updateUI = FALSE;
    }
}



// -----------------------------------------------------------------------------
    static void on_sci_notify(
        ScintillaObject *sci,
        gint scn,
        SCNotification *nt,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);

    switch(nt->nmhdr.code) {

        case(SCN_UPDATEUI): {

            if (nt->updated & SC_UPDATE_CONTENT) {

                if (is_curr_document(data)) {
                    render_document(sci, data);
                }
            }

            break;
        }

        case(SCN_MODIFIED):
        {
            if (nt->modificationType & SC_MOD_INSERTTEXT) {

                // if we insert into position that had bracket
                clear_bc_indicators(sci, nt->position, nt->length);

                /*
                 * Check to adjust current bracket positions
                 */

                for (gint bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                    if (
                        move_brackets(
                            sci,
                            *data,
                            nt->position, nt->length,
                            static_cast<BracketType>(bracketType)
                        )
                    ) {
                        data->updateUI = TRUE;
                    }
                }
            }

            if (nt->modificationType & SC_MOD_DELETETEXT) {

                for (gint bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                    if (
                        remove_brackets(
                            sci,
                            *data,
                            nt->position, nt->length,
                            static_cast<BracketType>(bracketType)
                        )
                    ) {
                        data->updateUI = TRUE;
                    }
                }
            }

            if (nt->modificationType & SC_MOD_CHANGESTYLE) {

                if (data->init == TRUE) {
                    for (gint bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                        if (data->bracketColorsEnable[bracketType] == FALSE) {
                            continue;
                        }
                        for (gint i = nt->position; i < nt->position + nt->length; i++) {
                            gchar currChar = sci_get_char_at(sci, i);
                            if (is_bracket_type(currChar, static_cast<BracketType>(bracketType))) {
                                //g_debug("%s: Handling style change for bracket at %d", __FUNCTION__, i);
                                data->recomputeIndicies.insert(i);
                            }
                        }
                    }
                }
            }

            break;
        }
    }
}



// -----------------------------------------------------------------------------
    gboolean render_brackets_timeout(
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    if (not has_document()) {
        return FALSE;
    }

    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);

    if (not is_curr_document(data)) {
        data->StopTimers();
        return FALSE;
    }

    /*
     * check if background color changed
     */

    ScintillaObject *sci = data->doc->editor->sci;
    guint32 currBGColor = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, BC_NO_ARG);
    if (currBGColor != data->backgroundColor) {
        g_debug("%s: background color changed: %#04x", __FUNCTION__, currBGColor);

        gboolean currDark = utils_is_dark(currBGColor);
        gboolean wasDark = utils_is_dark(data->backgroundColor);

        if (currDark != wasDark) {
            g_debug("%s: Need to change colors scheme!", __FUNCTION__);
            data->bracketColors = currDark ? sDarkBackgroundColors : sLightBackgroundColors;
            assign_indicator_colors(data);
        }

        data->backgroundColor = currBGColor;
    }

    if (data->updateUI) {
        render_document(sci, data);
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    gboolean recompute_brackets_timeout(
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    static const unsigned sIterationLimit = 50;

    if (not has_document()) {
        return FALSE;
    }

    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);
    if (not is_curr_document(data)) {
        data->StopTimers();
        return FALSE;
    }

    if (data->init == FALSE) {
        find_all_brackets(*data);
        data->init = TRUE;
    }

    if (data->recomputeIndicies.size()) {

        ScintillaObject *sci = data->doc->editor->sci;

        unsigned numIterations = 0;
        for (
            auto position = data->recomputeIndicies.begin();
            position != data->recomputeIndicies.end();
            numIterations++
        )
        {
            for (gint bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {

                BracketMap &bracketMap = data->bracketMaps[bracketType];

                if (
                    is_bracket_type(
                        sci_get_char_at(sci, *position),
                        static_cast<BracketType>(bracketType)
                    )
                ) {
                    // check if in a comment
                    if (is_ignore_style(data, *position)) {
                        bracketMap.mBracketMap.erase(*position);
                        clear_bc_indicators(sci, *position, 1);
                    }
                    else {
                        gint brace = compute_bracket_at(sci, bracketMap, *position);
                        if (brace >= 0) {
                            data->redrawIndicies.insert(brace);
                        }
                        data->updateUI = TRUE;
                    }
                }

                std::set<BracketMap::Index> updatedBrackets = bracketMap.ComputeOrder();
                data->redrawIndicies.insert(updatedBrackets.begin(), updatedBrackets.end());
            }

            position = data->recomputeIndicies.erase(position);
            if (numIterations >= sIterationLimit) {
                break;
            }
        }
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static void on_document_close(
        GObject *obj,
        GeanyDocument *doc,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    g_return_if_fail(DOC_VALID(doc));
    gpointer pluginData = plugin_get_document_data(geany_plugin, doc, sPluginName);
    if (pluginData != NULL) {
        BracketColorsData *data = reinterpret_cast<BracketColorsData *>(pluginData);
        data->StopTimers();
    }

    ScintillaObject *sci = doc->editor->sci;
    remove_bc_indicators(sci);
}



// -----------------------------------------------------------------------------
    static void on_document_activate(
        GObject *obj,
        GeanyDocument *doc,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    gpointer pluginData = plugin_get_document_data(geany_plugin, doc, sPluginName);
    if (pluginData != NULL) {
        BracketColorsData *data = reinterpret_cast<BracketColorsData *>(pluginData);
        data->StartTimers();
    }
}



// -----------------------------------------------------------------------------
    static void on_startup_complete(
        GObject *obj,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    GeanyDocument *currDoc = document_get_current();
    if (currDoc != NULL) {
        gpointer pluginData = plugin_get_document_data(geany_plugin, currDoc, sPluginName);
        if (pluginData != NULL) {
            BracketColorsData *data = reinterpret_cast<BracketColorsData *>(pluginData);
            data->StartTimers();
        }
    }
}


// -----------------------------------------------------------------------------
    static std::string get_config_filename(void)
/*

----------------------------------------------------------------------------- */
{
    std::string configFile(sPluginName);
    configFile.append(".conf");

    return std::string(
        g_build_filename(
            geany_data->app->configdir, "plugins",
            sPluginName, configFile.c_str(),
            NULL
        )
    );
}



// -----------------------------------------------------------------------------
    static gboolean read_keyfile(
        GKeyFile *kf,
        std::string filename,
        GKeyFileFlags flags
    )
/*
    loads filename in kf and return %FALSE if failed, emitting a warning
    unless the file was simply missing
----------------------------------------------------------------------------- */
{
    GError *error = NULL;
    if (!g_key_file_load_from_file(kf, filename.c_str(), flags, &error)) {
        if (error->domain != G_FILE_ERROR || error->code != G_FILE_ERROR_NOENT) {
            g_debug("%s: Failed to load configuration file: %s", __FUNCTION__, error->message);
        }
        g_error_free (error);
        return FALSE;
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static gboolean write_keyfile(
        GKeyFile *kf,
        std::string filename
    )
/*
    loads filename in kf and return %FALSE if failed, emitting a warning
    unless the file was simply missing
----------------------------------------------------------------------------- */
{
    gchar *dirname = g_path_get_dirname(filename.c_str());

    gsize length;
    gchar *data = g_key_file_to_data(kf, &length, NULL);

    GError *error = NULL;
    gint err;
    gboolean success = FALSE;

    if ((err = utils_mkdir(dirname, TRUE)) != 0) {
        g_warning(
            "Failed to create configuration directory \"%s\": %s",
            dirname, g_strerror(err)
        );
    }
    else if (!g_file_set_contents(filename.c_str(), data, (gssize)length, &error)) {
        g_warning("Failed to save configuration file: %s", error->message);
        g_error_free(error);
    } else {
        success = TRUE;
    }

    g_free(data);
    g_free(dirname);
    return success;
}



// -----------------------------------------------------------------------------
    static void load_config(void)
/*

----------------------------------------------------------------------------- */
{
    std::string filename = get_config_filename();
    GKeyFile *kf = g_key_file_new();

    g_debug("%s: Trying to load configuration file: %s", __FUNCTION__, filename.c_str());

    if (read_keyfile(kf, filename, G_KEY_FILE_NONE)) {
        for (auto &it : gPluginSettings) {
            it->read(kf);
        }
    }
    else {
        g_debug("%s: No configuration file yet", __FUNCTION__);
    }

    g_key_file_free(kf);
}


// -----------------------------------------------------------------------------
    static void save_config(void)
/*

----------------------------------------------------------------------------- */
{
    std::string filename = get_config_filename();
    GKeyFile *kf = g_key_file_new();

    g_debug("%s: Trying to save configuration file: %s", __FUNCTION__, filename.c_str());
    read_keyfile(kf, filename, G_KEY_FILE_KEEP_COMMENTS);

    for (auto &it : gPluginSettings) {
        it->write(kf);
    }
    write_keyfile(kf, filename);

    g_key_file_free(kf);
}



// -----------------------------------------------------------------------------
    static void on_document_open(
        GObject *obj,
        GeanyDocument *doc,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    g_return_if_fail(DOC_VALID(doc));

    BracketColorsData *data = bracket_colors_data_new(doc);
    ScintillaObject *sci = doc->editor->sci;
    data->doc = doc;

    plugin_signal_connect(
        geany_plugin,
        G_OBJECT(sci), "sci-notify",
        FALSE,
        G_CALLBACK(on_sci_notify), data
    );

    /*
     * Setup our bracket indicators
     */

    data->backgroundColor = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, BC_NO_ARG);
    if (utils_is_dark(data->backgroundColor)) {
        data->bracketColors = sDarkBackgroundColors;
    }

    assign_indicator_colors(data);

    if (user_data == NULL) {
        data->StartTimers();
    }

}



// -----------------------------------------------------------------------------
    static gboolean plugin_bracketcolors_init (
        GeanyPlugin *plugin,
        gpointer pdata
    )
/*

----------------------------------------------------------------------------- */
{
    g_log_set_writer_func(g_log_writer_default, NULL, NULL);

    geany_plugin = plugin;
    geany_data = plugin->geany_data;

    load_config();

    gboolean inInit = TRUE;

    guint i = 0;
    foreach_document(i)
    {
        on_document_open(NULL, documents[i], (gpointer) &inInit);
    }

    plugin_signal_connect(
        plugin,
        NULL, "document-activate",
        FALSE,
        G_CALLBACK(on_document_activate), NULL
    );

    on_startup_complete(NULL, (gpointer) &inInit);

    return TRUE;
}



// -----------------------------------------------------------------------------
    static void plugin_bracketcolors_cleanup (
        GeanyPlugin *plugin,
        gpointer pdata
    )
/*

----------------------------------------------------------------------------- */
{
    guint i = 0;
    foreach_document(i)
    {
        on_document_close(NULL, documents[i], NULL);
    }

    save_config();
}



// -----------------------------------------------------------------------------
    static PluginCallback plugin_bracketcolors_callbacks[] =
/*

----------------------------------------------------------------------------- */
{
    { "document-open",          (GCallback) &on_document_open,      FALSE, NULL },
    { "document-new",           (GCallback) &on_document_open,      FALSE, NULL },
    { "document-close",         (GCallback) &on_document_close,     FALSE, NULL },
    { "geany-startup-complete", (GCallback) &on_startup_complete,   FALSE, NULL },
    { NULL, NULL, FALSE, NULL }
};



// -----------------------------------------------------------------------------
    static void checkbox_toggled(
        GtkWidget *checkbox,
        gpointer data
    )
/*
    if checkbox toggled, block the button grid
----------------------------------------------------------------------------- */
{
    GtkWidget *colorButtonGrid = GTK_WIDGET(data);

    gboolean isActive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));

    gtk_widget_set_sensitive(
        colorButtonGrid,
        not isActive
    );

    gUseDefaults = isActive;
}



// -----------------------------------------------------------------------------
    static void color_button_set(
        GtkColorButton *colorButton,
        gpointer data
    )
/*

----------------------------------------------------------------------------- */
{
    //gchar *fullConfigFile = get_config_filename();
    //printf("Color changed! Writing to %s\n", fullConfigFile);
}



// -----------------------------------------------------------------------------
    static GtkWidget* plugin_bracketcolors_configure(
        GeanyPlugin *plugin,
        GtkDialog *dialog,
        gpointer pdata
    )
/*
    make UI elements
----------------------------------------------------------------------------- */
{
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

    GtkWidget *colorButtonGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(colorButtonGrid), 5);
    gtk_widget_set_margin_start(colorButtonGrid, 5);
    gtk_widget_set_margin_end(colorButtonGrid, 5);
    gtk_widget_set_margin_bottom(colorButtonGrid, 5);

    GtkWidget *frame = gtk_frame_new("Bracket Colors");
    gtk_container_add(GTK_CONTAINER(frame), colorButtonGrid);

    for (int i = 0; i < BC_NUM_COLORS; i++) {

        GtkWidget *colorButton = gtk_color_button_new();

        gtk_grid_attach(
            GTK_GRID(colorButtonGrid), colorButton,
            i, 0, 1, 1
        );

        g_signal_connect(
            G_OBJECT(colorButton),
            "color-set",
            G_CALLBACK(color_button_set),
            NULL
        );
    }

    gtk_grid_attach(
        GTK_GRID(grid), frame,
        0, 0, 1, 1
    );

    GtkWidget *checkBox = gtk_check_button_new_with_label("Use Defaults");
    gtk_grid_attach(
        GTK_GRID(grid), checkBox,
        0, 1, 1, 1
    );

    g_signal_connect(
        G_OBJECT(checkBox),
        "toggled",
        G_CALLBACK(checkbox_toggled),
        colorButtonGrid
    );

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkBox), gUseDefaults);

    return grid;
}



// -----------------------------------------------------------------------------
    extern "C" void geany_load_module(GeanyPlugin *plugin)
/*
    Load module
----------------------------------------------------------------------------- */
{
    main_locale_init(LOCALEDIR, GETTEXT_PACKAGE);

    /* Set metadata */
    plugin->info->name          = _("Bracket Colors");
    plugin->info->description   = _("Color nested brackets, braces, parenthesis");
    plugin->info->version       = "0.1";
    plugin->info->author        = "Asif Amin <asifamin@utexas.edu>";

    /* Set functions */
    plugin->funcs->init         = plugin_bracketcolors_init;
    plugin->funcs->cleanup      = plugin_bracketcolors_cleanup;
    plugin->funcs->callbacks    = plugin_bracketcolors_callbacks;
    plugin->funcs->configure    = plugin_bracketcolors_configure;

    /* Register! */
    GEANY_PLUGIN_REGISTER(plugin, 226);
}
