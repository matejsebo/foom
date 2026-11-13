/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
 *
 *    FOOM is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    FOOM is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef OPENORIENTEERING_SHORTCUT_SETTINGS_H
#define OPENORIENTEERING_SHORTCUT_SETTINGS_H

#include <QByteArray>
#include <QHash>
#include <QKeySequence>
#include <QString>

class QAction;
class QSettings;

namespace OpenOrienteering {

/**
 * Stored keyboard shortcuts of the editor actions, by action id.
 *
 * See the design notes, "Shortcut editor".
 */
class ShortcutSettings
{
public:
	using Actions = QHash<QByteArray, QAction*>;
	
	/** The settings group which holds the shortcuts. */
	static QString group();
	
	/** Reads the stored sequence of the action; returns false when none is stored. */
	static bool load(QSettings& settings, const QByteArray& id, QKeySequence& out);
	
	/** Stores the sequence of the action; an empty sequence stores "no shortcut". */
	static void store(QSettings& settings, const QByteArray& id, const QKeySequence& sequence);
	
	/** Removes the stored sequence of the action, so that its default applies. */
	static void remove(QSettings& settings, const QByteArray& id);
	
	/** Removes every stored sequence. */
	static void removeAll(QSettings& settings);
	
	/** Applies the stored sequences to the actions; returns how many were applied. */
	static int apply(QSettings& settings, const Actions& actions);
	
	/** Returns the action other than except which uses the sequence, or nullptr. */
	static QAction* conflict(const Actions& actions, const QKeySequence& sequence, const QAction* except);
	
	/** Returns the actions' shortcuts as {"shortcuts": {id: sequence}}. */
	static QByteArray toJson(const Actions& actions);
	
	/** Stores the sequences of such a document; returns their number, or -1 for a malformed document. */
	static int fromJson(QSettings& settings, const QByteArray& json);
};


}  // namespace OpenOrienteering

#endif
