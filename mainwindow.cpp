/*
    HRPT-Decoder, a software for processing NOAA-POES high resolution weather satellite images.
    Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Email: <postmaster@poes-weather.com>
    Web: <http://www.poes-weather.com>
*/
//---------------------------------------------------------------------------

#include <QtGui>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <math.h>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "config.h"
#include "plist.h"

#include "block.h"
#include "stationdialog.h"
#include "station.h"
#include "tledialog.h"
#include "orbitdialog.h"
#include "satpassdialog.h"
#include "trackwidget.h"
#include "imagewidget.h"
#include "activesatdialog.h"
#include "satpropdialog.h"
#include "rigdialog.h"
#include "gpsdialog.h"

#include "Satellite.h"
#include "satutil.h"
#include "utils.h"
#include "settings.h"
#include "rig.h"

#include "os.h"
#include "version.h"

#include "trackthread.h"
#include "cadusplitterdialog.h"

//---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
  ui->setupUi(this);

  blockImage = NULL;
  block      = new TBlock;

  qth       = new TStation;
  satList   = new PList;
  settings  = new TSettings;
  rig       = new TRig;
  gps       = NULL;
  opensat   = new TSat;

  QCoreApplication::setOrganizationName("poes-weather");
  QCoreApplication::setOrganizationDomain("poes-weather.com");
  QCoreApplication::setApplicationName("POES Weather Satellite Decoder");

  createPaths();

  exitAct = new QAction(tr("E&xit"), this);
  exitAct->setShortcut(tr("Ctrl+Q"));
  exitAct->setStatusTip(tr("Exit USRP-POES-Decoder"));
  connect(exitAct, SIGNAL(triggered()), this, SLOT(close()));
  ui->menuFile->addAction(exitAct);
  ui->actionSave_As->setEnabled(false);
  ui->actionClose->setEnabled(false);

  ui->menuView->addAction(ui->mainToolBar->toggleViewAction());
  ui->mainToolBar->setWindowTitle("Toolbar");

  imageLabel = new QLabel;

  imageLabel->setBackgroundRole(QPalette::Dark);
  imageLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  imageLabel->setScaledContents(false);

  ui->scrollArea->setWidget(imageLabel);
  ui->scrollArea->setBackgroundRole(QPalette::Dark);
  setCentralWidget(ui->scrollArea);

  imageWidget = new ImageWidget(this);
  ui->menuView->addAction(imageWidget->toggleViewAction());

  trackWidget = new TrackWidget(this);
  ui->menuView->addAction(trackWidget->toggleViewAction());  

  readSettings();
}

//---------------------------------------------------------------------------
MainWindow::~MainWindow()
{
    delete ui;

    delete block;
    delete imageLabel;

    if(blockImage)
       delete blockImage;

    delete qth;
    delete settings;
    delete rig;

    if(gps)
        delete gps;

    delete opensat;

    clearSatList(satList, 1);
}

//---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
   qDebug("...closing application...");

   writeSettings();

   event->accept();
}

//---------------------------------------------------------------------------
//
//      Application Settings
//
//---------------------------------------------------------------------------
void MainWindow::createPaths(void)
{
   mkpath(getConfPath());
   mkpath(getTLEPath());
   mkpath(getTLEPath(1));
}

//---------------------------------------------------------------------------
// flags&1 = silent
bool MainWindow::mkpath(const QString &path, int flags)
{
 QDir dir;

   if(!dir.exists(path) && !dir.mkdir(path)) {
       if(!(flags&1))
          QMessageBox::critical(this, "Failed to create directory!", path);

       return false;
   }
   else
       return true;
}

//---------------------------------------------------------------------------
QString MainWindow::getTLEPath(int type)
{
    if(type == 1)
        return qApp->applicationDirPath() + "/" + PATH_TLE_ARC;
    else
        return qApp->applicationDirPath() + "/" + PATH_TLE;
}

//---------------------------------------------------------------------------
QString MainWindow::getConfPath(void)
{
 return qApp->applicationDirPath() + "/" + PATH_CONF;
}

//---------------------------------------------------------------------------
void MainWindow::setCaption(const QString &filename)
{
 QString caption;

 caption.sprintf("%s-%s", VER_SWNAME_STR, VER_FILEVERSION_STR);
 if(!qth->name().isEmpty())
    caption += " @ " + qth->name();

 if(!filename.isEmpty())
    caption += " [" + filename + "]";

 setWindowTitle(caption);
}

//---------------------------------------------------------------------------
//
//      image functions
//
//---------------------------------------------------------------------------
void MainWindow::on_actionOpen_triggered()
{
 QFileDialog dialog(this);
 QString fileName;
 QStringList filters;
 int i, index;
 bool rc;

  dialog.setFileMode(QFileDialog::AnyFile);
  if(!FileName.isEmpty())
     dialog.selectFile(FileName);
  else
     dialog.setDirectory(QDir::currentPath());

  for(i=0; i<NUM_SUPPORTED_BLOCKS; i++)
     filters.append(block->getBlockTypeStr(i, 1));
  dialog.setNameFilters(filters);

  if(dialog.exec())
     fileName = dialog.selectedFiles().at(0);
  else
     return;

  index = filters.indexOf(dialog.selectedNameFilter());
  if(index < 0) {
     qDebug("Unsupported format: %s index: %d", dialog.selectedNameFilter().toStdString().c_str(), index);
     return;
  }

  qDebug("Filename: %s", fileName.toStdString().c_str());
  qDebug("Filter: %s, index: %d", dialog.selectedNameFilter().toStdString().c_str(), index);

  FileName = fileName;

 // QApplication::processEvents();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  rc = processData(FileName.toStdString().c_str(), index);

  // LRIT/HRIT must be uncompressed before rendering
  if(rc && block->isCompressed()) {
#if 0
      QApplication::restoreOverrideCursor();

      rc = false;

      i = QMessageBox::question(this, "Data is compressed", "In order to render the image it must be first uncompressed.\Uncompress to file?",
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

      if(i == QMessageBox::Yes) {
         fileName = QFileDialog::getSaveFileName(this, "Save as uncompressed", FileName);
         if(!fileName.isEmpty())
            rc = true;
      }

      QApplication::setOverrideCursor(Qt::WaitCursor);

      if(rc)
         rc = block->uncompress(fileName.toStdString().c_str());
#endif
  }

  if(rc)
     rc = renderImage();

  if(!rc) {
     imageLabel->clear();
     if(blockImage)
        delete blockImage;
     blockImage = NULL;
     block->close();
  }

  imageWidget->setFrames(block->getBlockTypeStr(index), block->getFrames());

  ui->actionSave_As->setEnabled(rc);
  ui->actionClose->setEnabled(rc);
  setCaption(FileName);

  QApplication::restoreOverrideCursor();
}

//---------------------------------------------------------------------------
bool MainWindow::processData(const char *filename, int blockType)
{
 QString str;
 TSat    *sat;
 bool    rc;

  if(!block->setBlockType((Block_Type) blockType)) {
     str.sprintf("Unsupported type: %s %s",
                 block->getBlockTypeStr(blockType).toStdString().c_str(),
                 filename);
     ui->statusBar->showMessage(str);

     return false;
  }

  // read satellite passinfo file
  block->satprop->zero();

  if(!(rc = opensat->ReadPassinfo(filename))) {
      str.sprintf("Warning: Couldn't read/find passinfo file (.ini)!");
      ui->statusBar->showMessage(str);

      // TODO: if passinfo file is not present exec a dialog where user can select a satellite
  }
  else {
      sat = getSat(satList, opensat->name);

      if(sat == NULL) {
          sat = new TSat(opensat);
          sat->sat_props->add_defaults(1);
          satList->Add(sat);
      }

      *block->satprop = *sat->sat_props;
  }

  imageWidget->setProperties(rc ? opensat->isNorthbound():imageWidget->isNorthbound());

  if(!block->open(filename)) {
     str.sprintf("No frames found in file %s", filename);
     ui->statusBar->showMessage(str);

     block->close();

     return false;
  }

  if(blockImage)
     delete blockImage;

  try {
     blockImage = new QImage(block->getWidth(), block->getHeight(), QImage::Format_RGB888);

     qDebug("width: %d height: %d", block->getWidth(), block->getHeight());
  }
  catch(...)
  {
     qDebug("Failed to create QImage");

     block->close();
     blockImage = NULL;
     ui->statusBar->showMessage("Failed to create QImage");

     return false;
  }

  return true;
}
//---------------------------------------------------------------------------
void MainWindow::on_actionSave_As_triggered()
{
 QFileDialog dialog(this);
 QString fileName, str;

 if(!blockImage || FileName.isEmpty())
     return;

 dialog.setAcceptMode(QFileDialog::AcceptSave);
 dialog.setNameFilter(getImageFormats());

 QFileInfo fi(FileName);
 dialog.setDirectory(fi.absoluteFilePath());
 fileName.sprintf("%s.png", fi.baseName().toStdString().c_str());
 dialog.selectFile(fileName);

 qDebug("Save Filename: %s" ,fileName.toStdString().c_str());

 if(!dialog.exec())
    return;

 QApplication::setOverrideCursor(Qt::WaitCursor);

 fileName = dialog.selectedFiles().at(0);

 if(blockImage->save(fileName, 0, 75))
    str.sprintf("Image saved: %s" ,fileName.toStdString().c_str());
 else
    str.sprintf("Failed to save image: %s" ,fileName.toStdString().c_str());

 ui->statusBar->showMessage(str);

 QApplication::restoreOverrideCursor();
}

//---------------------------------------------------------------------------
void MainWindow::on_actionClose_triggered()
{
    if(blockImage)
       delete blockImage;
    blockImage = NULL;
    block->close();

    ui->statusBar->showMessage("");
    setCaption();
    imageWidget->setVisible(false);

    imageLabel->setPixmap(NULL);
    ui->actionClose->setEnabled(false);
}

//---------------------------------------------------------------------------
QString MainWindow::getImageFormats(void)
{
 QString imFormats, format;
 int i;

  imFormats = "*.png *.bmp";
  for(i=0; i<QImageWriter::supportedImageFormats().count(); ++i)
  {
      format = QString(QImageWriter::supportedImageFormats().at(i)).toLower();
      if(format == "jpg")
         imFormats += " *.jpg *.jpeg";
      else if(format == "tif")
         imFormats += " *.tif *.tiff";
      else if(format == "gif")
         imFormats += " *.gif";
      qDebug(format.toStdString().c_str());
   }

 return imFormats;
}

//---------------------------------------------------------------------------
bool MainWindow::renderImage(void)
{
 bool rc;

  if(!blockImage)
     return false;

  QApplication::setOverrideCursor(Qt::WaitCursor);

  rc = block->toImage(blockImage);
  if(rc)
     imageLabel->setPixmap(QPixmap::fromImage(*blockImage));

  if(!imageWidget->isVisible())
     imageWidget->setVisible(true);

  QApplication::restoreOverrideCursor();

 return rc;
}
//---------------------------------------------------------------------------
//
//      Registry- and component settings
//
//---------------------------------------------------------------------------
void MainWindow::readSettings(void)
{
    QSettings reg(VER_COMPANYNAME_STR, VER_SWNAME_STR);

    QPoint    pos;
    QSize     size;
    bool      bval;
    uint      i, state;


    // NOTICE:
    // All component settings must first be read before any window settings
    // Possible errors should be reported last

    qth->readSettings(&reg);
    readSatelliteSettings();
    rig->readSettings(&reg);

    // window settings
    QDesktopWidget *desktop = QApplication::desktop();

    // center the window at first startup
    int screenwidth = desktop->width();
    int screenheight = desktop->height();
    int width = screenwidth * 3 / 4;
    int height = screenheight * 3 / 4;

    int x = (screenwidth - width) / 2;
    int y = (screenheight - height) / 2;

    reg.beginGroup("MainWindow");
      pos   = reg.value("pos", QPoint(x, y)).toPoint();
      size  = reg.value("size", QSize(width, height)).toSize();
      state = reg.value("state", 0).toInt();

      if(state & Qt::WindowMaximized) {
          setWindowState(Qt::WindowMaximized);
          setGeometry(x, y, width, height);
      }
      else {
          resize(size);
          move(pos);
      }
    reg.endGroup();

    reg.beginGroup("TrackWidget");
      pos  = reg.value("pos",  QPoint(x, y)).toPoint();
      size = reg.value("size", QSize(width, 120)).toSize();
      i    = reg.value("dock", Qt::TopDockWidgetArea).toUInt();
      addDockWidget((Qt::DockWidgetArea) i, trackWidget);

      trackWidget->setFloating(reg.value("floating", false).toBool());
      if(trackWidget->isFloating()) {
         trackWidget->resize(size);
         trackWidget->move(pos);
      }

      bval = reg.value("visible", false).toBool();
      if(bval)
          bval = countSats(1|2);

      trackWidget->setVisible(bval);
    reg.endGroup();

    reg.beginGroup("ImageWidget");
      pos =  reg.value("pos", QPoint(x, y)).toPoint();
      size = reg.value("size", QSize(600, 120)).toSize();
      i =    reg.value("dock", Qt::LeftDockWidgetArea).toUInt();
      addDockWidget((Qt::DockWidgetArea) i, imageWidget);

      imageWidget->setFloating(reg.value("floating", false).toBool());
      if(imageWidget->isFloating()) {
         imageWidget->resize(size);
         imageWidget->move(pos);
      }

      imageWidget->setVisible(false);
    reg.endGroup();

    // finally...
    setCaption();
    trackWidget->updateSatCb();

    countSats(2);
}

//---------------------------------------------------------------------------
void MainWindow::writeSettings(void)
{
    QSettings reg(VER_COMPANYNAME_STR, VER_SWNAME_STR);

    reg.beginGroup("MainWindow");
      reg.setValue("pos", pos());
      reg.setValue("size", size());
      uint i = windowState() & Qt::WindowMaximized;
      reg.setValue("state", i);
    reg.endGroup();

    reg.beginGroup("TrackWidget");
      reg.setValue("pos", trackWidget->pos());
      reg.setValue("size", trackWidget->size());
      reg.setValue("visible", trackWidget->isVisible());
      reg.setValue("floating", trackWidget->isFloating());
      reg.setValue("dock", dockWidgetArea(trackWidget));
    reg.endGroup();

    reg.beginGroup("ImageWidget");
      reg.setValue("pos", imageWidget->pos());
      reg.setValue("size", imageWidget->size());
      reg.setValue("visible", imageWidget->isVisible());
      reg.setValue("floating", imageWidget->isFloating());
      reg.setValue("dock", dockWidgetArea(imageWidget));
    reg.endGroup();

    qth->writeSettings(&reg);
    writeSatelliteSettings();
    rig->writeSettings(&reg);
}

//---------------------------------------------------------------------------
//
//      Preferences
//
//---------------------------------------------------------------------------
void MainWindow::on_actionRig_triggered()
{
  bool enbRotor = rig->rotor->flags & R_ROTOR_ENABLE ? true:false;

  RigDialog dlg(this);


  if(dlg.exec()) {
      trackWidget->restartThread();
  }
  else
      rig->rotor->flags |= enbRotor ? R_ROTOR_ENABLE:0;
}

//---------------------------------------------------------------------------
void MainWindow::on_actionGroundstation_triggered()
{
  QString ini = getConfPath() + "/" + FILE_STATIONS_INI;
  StationDialog dlg(ini, qth, this);

  if(dlg.exec())
      updateQTH();
}

//---------------------------------------------------------------------------
void MainWindow::updateQTH(void)
{
    TSat *sat;
    int i;

    for(i=0; i<satList->Count; i++) {
        sat = (TSat *) satList->ItemAt(i);
        sat->AssignObsInfo(qth);
    }

    if(satList->Count)
       trackWidget->restartThread();

    setCaption(blockImage != NULL ? FileName:"");
}

//---------------------------------------------------------------------------
TSettings *MainWindow::getSettings(void)
{
    return settings;
}

//---------------------------------------------------------------------------
TRig *MainWindow::getRig(void)
{
    return rig;
}

//---------------------------------------------------------------------------
//
//      Satellite
//
//---------------------------------------------------------------------------
void MainWindow::writeSatelliteSettings(void)
{
    QString   str, ini;
    TSat      *sat;
    int       i;

    if(!satList->Count)
        return;

    ini = getConfPath() + "/" + FILE_SAT_INI;
    QFile file(ini);
    if(file.exists(ini))
        file.remove();

    QSettings reg(ini, QSettings::IniFormat);

    qth->writeSettings(&reg);

    for(i=0; i<satList->Count; i++) {
       sat = (TSat *) satList->ItemAt(i);
       str.sprintf("Spacecraft_%d", i+1);

#if 0
          TNDVI *vi;
          for(int ii=0; ii<sat->sat_props->ndvilist->Count; ii++) {
              vi = (TNDVI *) sat->sat_props->ndvilist->ItemAt(ii);
          }
#endif

       reg.beginGroup(str);
          reg.setValue("Name",  sat->name);
          reg.setValue("TLE_1", sat->line1);
          reg.setValue("TLE_2", sat->line2);

          sat->sat_scripts->writeSettings(&reg);
          sat->sat_props->writeSettings(&reg);
       reg.endGroup();
    }
}

//---------------------------------------------------------------------------
void MainWindow::readSatelliteSettings(void)
{
 QString   ini, str, name, line1, line2;
 int       i;
 TSat      *sat;

   ini = getConfPath() + "/" + FILE_SAT_INI;
   QFile file(ini);
   if(!file.exists(ini)) {
       ini = getConfPath() + "/default-" + FILE_SAT_INI;
       if(!file.exists(ini))
           return;
   }

   QSettings reg(ini, QSettings::IniFormat);
   sat = new TSat;

   i = 1;
   while(true) {
      sat->Zero();
      str.sprintf("Spacecraft_%d", i++);

      reg.beginGroup(str);
         name  = reg.value("Name",  "").toString();
         line1 = reg.value("TLE_1", "").toString();
         line2 = reg.value("TLE_2", "").toString();

         if(name.isEmpty() || line1.isEmpty() || line2.isEmpty()) {
             reg.endGroup();
             break;
         }

      if(getSat(satList, name)) {
          reg.endGroup();
          continue;
      }

      if(sat->TLEKepCheck((char *)name.toStdString().c_str(),
                          (char *)line1.toStdString().c_str(),
                          (char *)line2.toStdString().c_str()))
      {
          sat->AssignObsInfo(qth);
          sat->sat_scripts->readSettings(&reg);
          sat->sat_props->readSettings(&reg);
          satList->Add(new TSat(sat));

#if 0
          TSat *sat2 = (TSat *) satList->Last();
          TNDVI *vi;
          for(int ii=0; ii<sat2->sat_props->ndvilist->Count; ii++) {
              vi = (TNDVI *) sat2->sat_props->ndvilist->ItemAt(ii);
          }
#endif
      }

      reg.endGroup();      
   }

 delete sat;
}

//---------------------------------------------------------------------------
// flags&1 = silent
//      &2 = check if any is active
bool MainWindow::countSats(int flags)
{
    if(satList->Count == 0) {
        if(!(flags&1))
           QMessageBox::critical(this, "No satellites yet downloaded!",
                                       "Click menu Satellite->Keplerian elements to download.");
        return false;
    }
    else if(!(flags & 2))
        return true;

    TSat *sat;
    int i;

    for(i=0; i<satList->Count; i++) {
       sat = (TSat *) satList->ItemAt(i);
       if(sat->isActive())
           return true;
   }

    if(!(flags & 1))
       QMessageBox::critical(this, "No active satellites!",
                                   "Click menu Satellite->Active satellites.");
    return false;
}

//---------------------------------------------------------------------------
void MainWindow::on_actionKeplerian_elements_triggered()
{
  tledialog dlg(satList, qth, this);

  if(dlg.exec())
      trackWidget->updateSatCb();
}

//---------------------------------------------------------------------------
void MainWindow::on_actionOrbit_data_triggered()
{
    if(countSats()) {
        orbitdialog dlg(satList, this);
        dlg.exec();
    }
}

//---------------------------------------------------------------------------
void MainWindow::on_actionPredict_triggered()
{
    if(countSats()) {
        satpassdialog dlg(satList, this);
        dlg.exec();
    }
}

//---------------------------------------------------------------------------
void MainWindow::on_actionActive_satellites_triggered()
{
    ActiveSatDialog dlg(this);

    if(dlg.exec())
        trackWidget->updateSatCb();
}

//---------------------------------------------------------------------------
void MainWindow::on_actionProperties_triggered()
{
    TSat *sat;

    SatPropDialog dlg(satList, this);

    // TODO: backup settings incase cancel is toggled
    if(countSats()) {
        if(dlg.exec()) {
            if(blockImage) {
                sat = getSat(satList, opensat->name);

                if(sat) {
                    *block->satprop = *sat->sat_props;
                    imageWidget->setProperties(opensat->isNorthbound());
                }
                renderImage();
            }
        }
    }

}

//---------------------------------------------------------------------------
PList *MainWindow::getSatList(void)
{
    return satList;
}

//---------------------------------------------------------------------------
TSat *MainWindow::getNextSat(double daynum_)
{
 TSat   *sat;
 PList  *list;
 double utc_daynum, now_utc_daynum, daynum;
 TSat   *nextsat = NULL;
 int    i, flags, ii, max_ii;

    if(!countSats(1))
        return NULL;

    QDateTime utc(QDateTime::currentDateTime().toUTC());
    now_utc_daynum = GetStartTime(utc);

    if(daynum_ != 0)
        utc_daynum = daynum_;
    else
        utc_daynum = now_utc_daynum;

    list = new PList;
    ii = 0;
    max_ii = 144; // search 24 x 6 hours (6 days) forward

    while(ii < max_ii) {
        // get one satellite pass from each active satellite
        for(i=0; i<satList->Count; i++) {
            sat = (TSat *)satList->ItemAt(i);
            if(!sat->isActive() || !sat->CalcAll(utc_daynum))
                continue;

#if 1
            flags = 0;
            // is it currently above qth?
            if(sat->aostime < utc_daynum && sat->lostime > utc_daynum) {
                sat->daynum = now_utc_daynum;
                sat->Calc();

                // is it approaching?
                if(sat->get_range_rate() < 0)
                    list->Add(sat);
                else
                    flags |= 1;
            }
            else if(sat->aostime >= utc_daynum)
                list->Add(sat);


            if(flags & 1) {
                // find next orbit pass
                while(sat->GetNextRiseTime(utc, 1) > 0) {
                    if(!sat->CalcAll(sat->aostime))
                        break;
                    if(sat->aostime >= utc_daynum) {
                        list->Add(sat);
                        break;
                    }
                }
            }
#else
            // is it currently above qth?
            if(sat->aostime < utc_daynum && sat->lostime > utc_daynum)
                list->Add(sat);
            else if(sat->aostime >= utc_daynum)
                list->Add(sat);
            else {
                // find next orbit pass
                while(sat->GetNextRiseTime(utc, 1) > 0) {
                    if(!sat->CalcAll(sat->aostime))
                        break;
                    if(sat->aostime >= utc_daynum) {
                        list->Add(sat);
                        break;
                    }
                }
            }
#endif
        }

        if(list->Count)
            break;
        else {
            utc = utc.addSecs(60);
            utc_daynum = GetStartTime(utc);
            ii++;
        }
    }

    // find the satellite that is closest to qth
    daynum = 1e20;
    nextsat = NULL;
    for(i=0; i<list->Count; i++) {
        sat = (TSat *) list->ItemAt(i);
        if(sat->aostime < daynum) {
            nextsat = sat;
            daynum = sat->aostime;
        }
    }

    // check if there is an overlap.
    // if so select the satellite with higher elevation
    if(nextsat) {
        for(i=0; i<list->Count; i++) {
            sat = (TSat *) list->ItemAt(i);
            if(sat == nextsat)
                continue;

            if(sat->sat_max_ele > nextsat->sat_max_ele) {
                // overlap check
                flags = 0;
                if(sat->aostime < nextsat->aostime) {
                    // partial (happens before) || full overlap
                    if(sat->lostime > nextsat->aostime || sat->lostime > nextsat->lostime)
                        flags |= 1;
                }
                else if(sat->aostime < nextsat->lostime && sat->lostime > nextsat->lostime) {
                    // partial overlap happens after
                    flags |= 1;
                }

                if(flags & 1)
                    nextsat = sat;
            }
        }

        nextsat->CheckThresholds(rig);
    }

    delete list;

    return nextsat;
}

//---------------------------------------------------------------------------
// no need to check pass thresholds here, this toggled by the user
TSat *MainWindow::getNextSatByName(const QString &name, double daynum_)
{
 TSat   *sat;
 double utc_daynum, now_utc_daynum;
 int    flags;

    if(!countSats(1) || name.isEmpty())
        return NULL;

    sat = getSat(satList, name);
    if(sat == NULL)
        return NULL;

    QDateTime utc(QDateTime::currentDateTime().toUTC());
    now_utc_daynum = GetStartTime(utc);

    if(daynum_ != 0)
        utc_daynum = daynum_;
    else
        utc_daynum = now_utc_daynum;

    if(!sat->CalcAll(utc_daynum))
       return NULL;

    flags = 0;
    if(sat->aostime >= utc_daynum)
        return sat;
    // is it currently above my horizon?
    if(sat->aostime <= utc_daynum && sat->lostime > utc_daynum) {
        sat->daynum = now_utc_daynum;
        sat->Calc();

        // is it approaching?
        if(sat->get_range_rate() < 0)
            return sat;
        else
            flags |= 1;
    }
    else
        flags |= 1;

    if(flags & 1) {
        while(sat->GetNextRiseTime(utc, 1) > 0) {
           if(!sat->CalcAll(sat->aostime))
              return NULL;
           if(sat->aostime >= utc_daynum)
              return sat;
        }
    }

 return NULL;
}

//---------------------------------------------------------------------------
//
//                  TOOLS
//
//---------------------------------------------------------------------------
void MainWindow::on_actionGPS_triggered()
{
    if(gps == NULL)
        gps = new GPSDialog(getConfPath() + "/" + FILE_GPS_INI, this);

    gps->show();
}

//---------------------------------------------------------------------------
void MainWindow::on_actionSplit_CADU_to_file_triggered()
{
#if 0

    CADUSplitterDialog dlg(this);

    dlg.exec();

#else
    CADUSplitterDialog *dlg = new CADUSplitterDialog(this);

    dlg->exec();

    delete dlg;
#endif
}

//---------------------------------------------------------------------------
