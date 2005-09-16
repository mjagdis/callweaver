/*
 * OpenPBX -- A telephony toolkit for Linux.
 *
 * KDE Console monitor -- Header file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <kapp.h>
#include <ktmainwindow.h>
#include <qpushbutton.h>
#include <kmenubar.h>
#include <qpopupmenu.h>
#include <qlistbox.h>
#include <qlayout.h>
#include <qframe.h>

class KOpenPBXConsole : public KTMainWindow
{
	Q_OBJECT
public:
	KOpenPBXConsole();
	void closeEvent(QCloseEvent *);
	QListBox *verbose;
public slots:
	void slotExit();
private:
	void KOpenPBXConsole::verboser(char *stuff, int opos, int replacelast, int complete);
	QPushButton *btnExit;
	KMenuBar *menu;
	QPopupMenu *file, *help;
};
