/*
** Copyright (c) 2006 - present, Alexis Megas.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from BiblioteQ without specific prior written permission.
**
** BIBLIOTEQ IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** BIBLIOTEQ, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QProcess>
#include <QSettings>
#include <QShortcut>
#include <QSqlRecord>

#include "biblioteq.h"
#include "biblioteq_documentationwindow.h"
#include "biblioteq_files.h"
#include "biblioteq_otheroptions.h"
#include "biblioteq_query_history.h"
#include "biblioteq_sqlite_merge_databases.h"
#include "biblioteq_swifty.h"

QColor biblioteq::itemMandatoryFieldColor(void) const
{
  return m_otheroptions->itemMandatoryFieldColor();
}

QColor biblioteq::itemQueryResultColor(void) const
{
  return m_otheroptions->itemQueryResultColor();
}

QHash<QString, QString> biblioteq::otherImagesHash(void) const
{
  return m_otherImages;
}

QMap<QString, QKeySequence> biblioteq::shortcuts(void) const
{
  QMap<QString, QKeySequence> map;

  foreach(auto action, findChildren<QAction *> ())
    if(action && action->shortcut().isEmpty() == false)
      map[action->text()] = action->shortcut();

  foreach(auto shortcut, findChildren<QShortcut *> ())
    if(shortcut && shortcut->key().isEmpty() == false)
      map[shortcut->key().toString()] = shortcut->key();

  return map;
}

QString biblioteq::formattedISBN10(const QString &str) const
{
  return m_otheroptions->isbn10DisplayFormat(str);
}

QString biblioteq::formattedISBN13(const QString &str) const
{
  return m_otheroptions->isbn13DisplayFormat(str);
}

QStringList biblioteq::selectedISBN10s(void) const
{
  QStringList list;

  foreach(const auto &index, ui.table->selectionModel()->selectedRows())
    {
      auto string = biblioteq_misc_functions::getColumnString
	(ui.table, index.row(), ui.table->columnNumber("ISBN-10")).trimmed();

      if(!string.isEmpty())
	list << string;
    }

  return list;
}

bool biblioteq::canAccessDatabaseEnumerations(void) const
{
  if(!m_db.isOpen())
    return false;
  else if(m_db.driverName() == "QSQLITE")
    return true;
  else if(m_roles.contains("administrator") ||
	  m_roles.contains("librarian"))
    return true;
  else
    return false;
}

bool biblioteq::isCurrentItemAPhotograph(void) const
{
  return biblioteq_misc_functions::getColumnString
    (ui.table,
     ui.table->currentRow(),
     ui.table->columnNumber("Type")) == "Photograph Collection";
}

bool biblioteq::showBookReadStatus(void) const
{
  return m_otheroptions->showBookReadStatus();
}

bool biblioteq::showMainTableImages(void) const
{
  return m_otheroptions->showMainTableImages();
}

void biblioteq::executeCustomQuery(QWidget *widget, const QString &text)
{
  auto querystr(text.trimmed());

  if(querystr.isEmpty())
    {
      QMessageBox::critical(widget,
			    tr("BiblioteQ: User Error"),
			    tr("Please provide a valid SQL statement."));
      QApplication::processEvents();
      return;
    }

  const auto &q(querystr.toLower());

  if(q.startsWith("alter ") ||
     q.startsWith("cluster ") ||
     q.startsWith("create " ) ||
     q.startsWith("drop ") ||
     q.startsWith("grant ") ||
     q.startsWith("insert ") ||
     q.startsWith("lock ") ||
     q.startsWith("revoke ") ||
     q.startsWith("truncate "))
    {
      QMessageBox::critical
	(widget,
	 tr("BiblioteQ: User Error"),
	 tr("Please provide a non-destructive SQL statement."));
      QApplication::processEvents();
    }
  else if(q.startsWith("delete "))
    {
      if(QMessageBox::
	 question(widget,
		  tr("BiblioteQ: Question"),
		  tr("Are you sure that you wish to execute the statement?"),
		  QMessageBox::No | QMessageBox::Yes,
		  QMessageBox::No) == QMessageBox::No)
	{
	  QApplication::processEvents();
	  return;
	}
      else
	QApplication::processEvents();

      QSqlQuery query(m_db);

      if(query.exec(querystr))
	slotRefresh();
    }
  else if(q.startsWith("update "))
    {
      if(QMessageBox::
	 question(widget,
		  tr("BiblioteQ: Question"),
		  tr("Are you sure that you wish to execute the statement?"),
		  QMessageBox::No | QMessageBox::Yes,
		  QMessageBox::No) == QMessageBox::No)
	{
	  QApplication::processEvents();
	  return;
	}
      else
	QApplication::processEvents();

      QSqlQuery query(m_db);
      auto list(ui.table->selectionModel()->selectedRows());

      if(list.isEmpty())
	{
	  if(query.exec(querystr))
	    slotRefresh();
	}
      else
	{
	  querystr.append(" WHERE myoid IN (");

	  auto column = ui.table->columnNumber("MYOID");

	  foreach(const auto &index, list)
	    {
	      auto item = ui.table->item(index.row(), column);

	      if(item)
		{
		  querystr.append(item->text());
		  querystr.append(", ");
		}
	    }

	  if(querystr.endsWith(", "))
	    querystr = querystr.mid(0, querystr.length() - 2);

	  querystr.append(")");

	  if(query.exec(querystr))
	    slotRefresh();
	}
    }
  else
    (void) populateTable(CUSTOM_QUERY, "Custom", querystr);
}

void biblioteq::populateFavorites(void)
{
  QApplication::setOverrideCursor(Qt::WaitCursor);
  ui.menu_Custom_Query->clear();

  QSettings settings;
  auto favorite(settings.value("custom_query_favorite").toString().trimmed());
  auto shortcut
    (settings.value("custom_query_favorite_shortcut").toString().trimmed());

  settings.beginGroup("customqueries");

  foreach(const auto &key, settings.childKeys())
    if(!key.trimmed().isEmpty() && key != tr("(Empty)"))
      {
	QAction *action = nullptr;
	QString summary("");
	auto k(key.mid(0, static_cast<int> (Limits::FAVORITES_LENGTH)).
	       remove('\n').remove('\r'));

	action = ui.menu_Custom_Query->addAction
	  (k, this, SLOT(slotCustomQuery(void)));
	action->setProperty("name", k);
	summary = QString::fromUtf8
	  (QByteArray::fromBase64(settings.value(k).toByteArray()));

	if(summary.length() > 32)
	  summary = summary.left(16) + "..." + summary.right(16);

	if(favorite == k)
	  action->setShortcut(QKeySequence(shortcut));
	else
	  action->setShortcut(QKeySequence());

	action->setText(QString("%1 (%2)").arg(k).arg(summary));
      }

  if(ui.menu_Custom_Query->actions().isEmpty())
    ui.menu_Custom_Query->addAction(tr("(Empty)"));

  QApplication::restoreOverrideCursor();
}

void biblioteq::prepareCustomQueryFavoriteShortcut(void)
{
  QSettings settings;
  auto favorite(settings.value("custom_query_favorite").toString().trimmed());
  auto shortcut
    (settings.value("custom_query_favorite_shortcut").toString().trimmed());

  foreach(auto action, ui.menu_Custom_Query->actions())
    if(action && action->property("name").toString() == favorite)
      action->setShortcut(QKeySequence(shortcut));
    else if(action)
      action->setShortcut(QKeySequence());
}

void biblioteq::prepareExternalApplicationsMenu(void)
{
  ui.menuExternal_Applications->clear();

  QMapIterator<QString, char> it(m_specialExecutables);

  while(it.hasNext())
    {
      it.next();
      ui.menuExternal_Applications->addAction
	(it.key(), this, SLOT(slotSpecialApplication(void)));
    }

  if(ui.menuExternal_Applications->isEmpty())
    ui.menuExternal_Applications->addAction(tr("Empty"));
}

void biblioteq::prepareTearOffMenus(void)
{
  createConfigToolMenu();
  m_configToolMenu->setTearOffEnabled(ui.actionTearOffMenus->isChecked());
  ui.menu_Edit->setTearOffEnabled(ui.actionTearOffMenus->isChecked());
  ui.menu_Options->setTearOffEnabled(ui.actionTearOffMenus->isChecked());
  ui.menu_Tools->setTearOffEnabled(ui.actionTearOffMenus->isChecked());
  ui.menu_View->setTearOffEnabled(ui.actionTearOffMenus->isChecked());
}

void biblioteq::prepareUpgradeNotification(void)
{
  if(!m_db.isOpen())
    return;

  /*
  ** Display a warning if the current database's schema is not current.
  */

  auto record(m_db.record("book"));

  if(!(record.indexOf("date_of_reform") >= 0))
    {
      if(m_db.driverName() == "QPSQL")
	QMessageBox::critical
	  (this,
	   tr("BiblioteQ: Database Error"),
	   tr("The current PostgreSQL schema must be updated. "
	      "Please execute the statement(s) in %1 for version %2.").
	   arg("postgresql_update_schema.sql").
	   arg(BIBLIOTEQ_VERSION));
      else
	QMessageBox::critical
	  (this,
	   tr("BiblioteQ: Database Error"),
	   tr("The current SQLite schema must be updated. "
	      "Tools -> Upgrade SQLite Schema (Recent)."));

      QApplication::processEvents();
    }
}

void biblioteq::slotAnimateAbout(void)
{
  if(m_aboutColors.first.lightness() >= 100)
    m_aboutTimer.stop();
  else
    {
      m_aboutColors.first = m_aboutColors.first.lighter(105);
      m_aboutColors.second = m_aboutColors.second.lighter(105);
    }

  m_about->setStyleSheet
    (QString("QDialog {background: qlineargradient(y1: 0, y2: 1, "
	     "stop: 0.45 %1, "
	     "stop: 0.46 %2);} "
	     "QWidget {color: white;}").
     arg(m_aboutColors.first.name()).arg(m_aboutColors.second.name()));
}

void biblioteq::slotContributors(void)
{
  if(!m_contributors)
    {
      m_contributors = new biblioteq_documentationwindow(this);
      m_contributors->setHtml
	(tr("<html>"
	    "<b>The following people have made BiblioteQ beautiful. "
	    "If your name is not mentioned, please submit a ticket at "
	    "https://github.com/textbrowser/biblioteq/issues. "
	    "Thank You!</b><br><br>"
	    "Ana Monteiro<br>"
	    "Arti<br>"
	    "Bill Burns<br>"
	    "Ceres<br>"
	    "Csanád Baksay<br>"
	    "Fares Othman<br>"
	    "Fátima Dias<br>"
	    "Frans<br>"
	    "J. Cornavin<br>"
	    "Jeepee<br>"
	    "Lazaros S.<br>"
	    "Leif-W<br>"
	    "Marty<br>"
	    "Nick<br>"
	    "Nicola<br>"
	    "ResetFlag<br>"
	    "Safranil<br>"
	    "SigmaX<br>"
	    "a12554<br>"
	    "caalma<br>"
	    "coldacid<br>"
	    "duhow<br>"
	    "fmra357<br>"
	    "jerzyPL<br>"
	    "jferby<br>"
	    "luismontilla<br>"
	    "meteos77<br>"
	    "numibesi<br>"
	    "ozgurcan<br>"
	    "robindegen<br>"
	    "sidheban<br>"
	    "sit42<br>"
	    "sunyuyangg555<br>"
	    "tamascz<br>"
	    "wohali<br>"
	    "yasbean"
	    "</html>"));
      m_contributors->setWindowTitle(tr("BiblioteQ: Contributors"));
    }

  m_contributors->show();
}

void biblioteq::slotCustomQuery(void)
{
  if(!m_db.isOpen())
    return;

  auto action = qobject_cast<QAction *> (sender());

  if(!action)
    return;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  QSettings settings;
  QString text(action->property("name").toString());
  auto string
    (QString::
     fromUtf8(QByteArray::
	      fromBase64(settings.
			 value(QString("customqueries/%1").arg(text)).
			 toByteArray()).constData()));

  QApplication::restoreOverrideCursor();
  executeCustomQuery(this, string);
}

void biblioteq::slotExecuteQuery(const QString &text)
{
  if(text.trimmed().isEmpty())
    return;

  (void) populateTable(CUSTOM_QUERY, "Custom", text);
}

void biblioteq::slotExportAsPNG(void)
{
  QFileDialog dialog(this);

  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setDefaultSuffix("png");
  dialog.setDirectory(QDir::homePath());
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setNameFilter(tr("PNG (*.png)"));
  dialog.setOption(QFileDialog::DontUseNativeDialog);
  dialog.setWindowTitle(tr("BiblioteQ: Export Icons View As PNG"));
  dialog.exec();
  QApplication::processEvents();

  if(dialog.result() == QDialog::Accepted)
    {
      QApplication::setOverrideCursor(Qt::WaitCursor);

      QImage image
	(ui.graphicsView->scene()->itemsBoundingRect().size().toSize(),
	 QImage::Format_RGB32);
      QPainter painter;

      image.fill(Qt::white);
      painter.begin(&image);
      ui.graphicsView->scene()->render
	(&painter,
	 QRectF(),
	 ui.graphicsView->scene()->itemsBoundingRect());
      painter.end();
      image.save(dialog.selectedFiles().value(0), "PNG", 100);
      QApplication::restoreOverrideCursor();
    }
}

void biblioteq::slotExportMembersHistoryAsCSV(void)
{
  exportAsCSV
    (history.table, m_members_diag, tr("BiblioteQ: Member's History As CSV"));
}

void biblioteq::slotGenerateAndCopyMemberLetter(void)
{
  auto clipboard = QApplication::clipboard();

  if(!clipboard)
    return;

  QSettings settings;
  QString str
    (QString::
     fromUtf8(QByteArray::
	      fromBase64(settings.value("otheroptions/generated_letter").
			 toByteArray()).constData()));

  str.replace("%1", userinfo_diag->m_userinfo.lastName->text().trimmed());
  str.replace("%2", userinfo_diag->m_userinfo.firstName->text().trimmed());
  str.replace("%3", userinfo_diag->m_userinfo.membershipfees->text());
  clipboard->setText(str);
}

void biblioteq::slotLaunchEmailSoftware(void)
{
  QSettings settings;
  QString str
    (QString::
     fromUtf8(QByteArray::
	      fromBase64(settings.value("otheroptions/generated_letter").
			 toByteArray()).constData()));

  str.replace("%1", userinfo_diag->m_userinfo.lastName->text().trimmed());
  str.replace("%2", userinfo_diag->m_userinfo.firstName->text().trimmed());
  str.replace("%3", userinfo_diag->m_userinfo.membershipfees->text());
  QDesktopServices::openUrl
    (QUrl::fromUserInput(QString("mailto:%1?body=%2&subject=BiblioteQ").
			 arg(userinfo_diag->m_userinfo.email->text().trimmed()).
			 arg(str)));
}

void biblioteq::slotMembersPagesChanged(int value)
{
  QSettings settings;

  settings.setValue("membersPerPage", value);
}

void biblioteq::slotMergeSQLiteDatabases(void)
{
  if(!m_sqliteMergeDatabases)
    m_sqliteMergeDatabases = new biblioteq_sqlite_merge_databases(this);

#ifdef Q_OS_ANDROID
  m_sqliteMergeDatabases->showMaximized();
#else
  biblioteq_misc_functions::center(m_sqliteMergeDatabases, this);
  m_sqliteMergeDatabases->showNormal();
#endif
  m_sqliteMergeDatabases->activateWindow();
  m_sqliteMergeDatabases->raise();
}

void biblioteq::slotPrintIconsView(void)
{
  if(menuBar())
    menuBar()->repaint();

  QApplication::processEvents();

  QPrinter printer;

  printer.setColorMode(QPrinter::Color);
  printer.setOutputFormat(QPrinter::PdfFormat);

  QScopedPointer<QPrintDialog> dialog(new QPrintDialog(&printer, this));

  if(dialog->exec() == QDialog::Accepted)
    {
      QApplication::processEvents();
      QApplication::setOverrideCursor(Qt::WaitCursor);

      QPainter painter(&printer);

      painter.setRenderHint(QPainter::Antialiasing);
      ui.graphicsView->scene()->render(&painter);
      QApplication::restoreOverrideCursor();
    }

  QApplication::processEvents();
}

void biblioteq::slotSaveGeneralSearchCaseSensitivity(bool state)
{
  QSettings settings;

  settings.setValue("generalSearchCaseSensitivity", state);
}

void biblioteq::slotSetMembershipFees(void)
{
  auto ok = true;
  double value = 0.0;

  value = QInputDialog::getDouble(m_members_diag,
				  tr("BiblioteQ: Set Membership Dues"),
				  tr("Membership Dues"),
				  0.0,
				  0.0,
				  std::numeric_limits<double>::max(),
				  2,
				  &ok);

  if(!ok)
    return;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  QSqlQuery query(m_db);
  auto list(bb.table->selectionModel()->selectedRows(0));

  if(list.isEmpty())
    {
      query.prepare("UPDATE member SET membership_fees = ?");
      query.addBindValue(value);

      if(!query.exec())
	{
	  QApplication::restoreOverrideCursor();
	  addError
	    (tr("Database Error"),
	     tr("Unable to update the entries."),
	     query.lastError().text(),
	     __FILE__,
	     __LINE__);
	  QMessageBox::critical
	    (this,
	     tr("BiblioteQ: Database Error"),
	     tr("Unable to update the entries."));
	  QApplication::processEvents();
	}
      else
	QApplication::restoreOverrideCursor();
    }
  else
    {
      foreach(const auto &index, list)
	{
	  auto str(index.data().toString());

	  query.prepare
	    ("UPDATE member SET membership_fees = ? WHERE memberid = ?");
	  query.addBindValue(value);
	  query.addBindValue(str);

	  if(!query.exec())
	    addError
	      (tr("Database Error"),
	       tr("Unable to update the entry %1.").arg(str),
	       query.lastError().text(),
	       __FILE__,
	       __LINE__);
	}

      QApplication::restoreOverrideCursor();
    }
}

void biblioteq::slotShowBatchActivitiesBrowser(void)
{
  m_batchActivities->show(this);
}

void biblioteq::slotShowDocumentation(void)
{
  auto action = qobject_cast<QAction *> (sender());

  if(!action)
    return;

  repaint();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  if(!m_documentation.value(action))
    {
      m_documentation[action] = new biblioteq_documentationwindow(this);

      QFile file;

      if(action == ui.action_English)
	file.setFileName(":/BiblioteQ.html");
      else
	{
#ifdef Q_OS_ANDROID
	  file.setFileName("assets:/BiblioteQ-Fr.html");
#else
	  file.setFileName
	    (QString(":/Contributed%1French%1BiblioteQ-Fr.html").
	     arg(QDir::separator()));
#endif
	}

      if(file.open(QIODevice::ReadOnly))
	m_documentation.value(action)->setHtml(file.readAll());
    }

  m_documentation.value(action)->show();
  QApplication::restoreOverrideCursor();
}

void biblioteq::slotShowFiles(void)
{
  if(!m_files)
    m_files = new biblioteq_files(this);

#ifdef Q_OS_ANDROID
  m_files->showMaximized();
#else
  biblioteq_misc_functions::center(m_files, this);
  m_files->showNormal();
#endif
  m_files->activateWindow();
  m_files->raise();
}

void biblioteq::slotShowMagic(void)
{
  m_magic->show(this);
}

void biblioteq::slotShowQueryHistory(void)
{
#ifdef Q_OS_ANDROID
  m_queryHistory->showMaximized();
#else
  biblioteq_misc_functions::center(m_queryHistory, this);
  m_queryHistory->showNormal();
#endif
  m_queryHistory->activateWindow();
  m_queryHistory->raise();
}

void biblioteq::slotShowReleaseNotes(void)
{
  auto action = qobject_cast<QAction *> (sender());

  if(!action)
    return;

  repaint();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  if(!m_releaseNotes.value(action))
    {
      if(action == ui.action_French_Release_Notes)
	m_releaseNotes[action] = new biblioteq_documentationwindow
	  (QUrl("qrc:/Contributed/French/Release-Notes-French.html"), this);
      else
	m_releaseNotes[action] = new biblioteq_documentationwindow
	  (QUrl("qrc:/Release-Notes.html"), this);

      m_releaseNotes.value(action)->setWindowTitle
	(tr("BiblioteQ: Release Notes"));
    }

  m_releaseNotes.value(action)->show();
  QApplication::restoreOverrideCursor();
}

void biblioteq::slotSpecialApplication(void)
{
  auto action = qobject_cast<QAction *> (sender());

  if(!action)
    return;

  qputenv("BIBLIOTEQ_DATABASE_NAME", m_db.databaseName().toUtf8());
  QProcess::startDetached(action->text(), QStringList());
  qunsetenv("BIBLIOTEQ_DATABASE_NAME");
}

void biblioteq::slotTableFindNext(void)
{
  if(!m_db.isOpen() || ui.stackedWidget->currentIndex() != 1)
    return;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  if(m_findList.isEmpty())
    m_findList = ui.table->findItems
      (ui.find->text().trimmed(), Qt::MatchContains);

  if(!m_findList.isEmpty())
    {
      ui.table->scrollToItem(ui.table->item(m_findList.at(0)->row(), 0));
      ui.table->selectRow(m_findList.at(0)->row());
      m_findList.removeFirst();
    }

  QApplication::restoreOverrideCursor();
}

void biblioteq::slotTableFindTextCleared(const QString &text)
{
  Q_UNUSED(text);
  m_findList.clear();
}

void biblioteq::slotTearOffMenus(void)
{
  prepareTearOffMenus();
}

void biblioteq::slotSwifty(void)
{
  if(!statusBar())
    return;

  auto version1(QString(BIBLIOTEQ_VERSION).remove('.').toLong());
  auto version2(m_swifty->newest_version().remove('.').trimmed().toLong());

  if(version1 < version2)
    {
      QTimer::singleShot(10000, this, SLOT(slotSwifty(void)));
      statusBar()->showMessage
	(tr("A new version %1 of BiblioteQ is available!").
	 arg(m_swifty->newest_version().trimmed()),
	 7500);
    }
}
