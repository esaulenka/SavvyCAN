#include "dbcsignaleditor.h"
#include "ui_dbcsignaleditor.h"
#include <QDateTime>
#include <QDebug>
#include <QMenu>
#include <QSettings>
#include <qevent.h>
#include "helpwindow.h"

DBCSignalEditor::DBCSignalEditor(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DBCSignalEditor)
{
    ui->setupUi(this);

    readSettings();

    qsrand(QDateTime::currentMSecsSinceEpoch());

    dbcHandler = DBCHandler::getReference();
    dbcMessage = nullptr;
    currentSignal = nullptr;

    QStringList headers2;
    headers2 << "Value" << "Text";
    ui->valuesTable->setColumnCount(2);
    ui->valuesTable->setColumnWidth(0, 200);
    ui->valuesTable->setColumnWidth(1, 440);
    ui->valuesTable->setHorizontalHeaderLabels(headers2);
    ui->valuesTable->horizontalHeader()->setStretchLastSection(true);

    ui->comboType->addItem("UNSIGNED INTEGER");
    ui->comboType->addItem("SIGNED INTEGER");
    ui->comboType->addItem("SINGLE PRECISION");
    ui->comboType->addItem("DOUBLE PRECISION");
    ui->comboType->addItem("STRING");

    connect(ui->signalsList, SIGNAL(currentRowChanged(int)), this, SLOT(clickSignalList(int)));
    connect(ui->bitfield, SIGNAL(gridClicked(int,int)), this, SLOT(bitfieldClicked(int,int)));
    connect(ui->signalsList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCustomMenuSignals(QPoint)));
    ui->signalsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->valuesTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCustomMenuValues(QPoint)));
    ui->valuesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->valuesTable, SIGNAL(cellChanged(int,int)), this, SLOT(onValuesCellChanged(int,int)));

    //now with 100% more lambda expressions just to make it interesting (and shorter, and easier...)
    connect(ui->cbIntelFormat, &QCheckBox::toggled,
            [=]()
            {
                if (currentSignal == nullptr) return;
                currentSignal->intelByteOrder = ui->cbIntelFormat->isChecked();
                fillSignalForm(currentSignal);
            });

    connect(ui->comboReceiver, &QComboBox::currentTextChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                currentSignal->receiver = dbcFile->findNodeByName(ui->comboReceiver->currentText());
            });
    connect(ui->comboType, &QComboBox::currentTextChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                switch (ui->comboType->currentIndex())
                {
                case 0:
                    currentSignal->valType = UNSIGNED_INT;
                    break;
                case 1:
                    currentSignal->valType = SIGNED_INT;
                    break;
                case 2:
                    currentSignal->valType = SP_FLOAT;
                    if (currentSignal->startBit > 39) currentSignal->startBit = 39;
                    currentSignal->signalSize = 32;
                    break;
                case 3:
                    currentSignal->valType = DP_FLOAT;
                    currentSignal->startBit = 7; //has to be!
                    currentSignal->signalSize = 64;
                    break;
                case 4:
                    currentSignal->valType = STRING;
                    break;                    
                }
                fillSignalForm(currentSignal);
            });
    connect(ui->txtBias, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtBias->text().toDouble(&result);
                if (result) currentSignal->bias = temp;
            });

    connect(ui->txtMaxVal, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtMaxVal->text().toDouble(&result);
                if (result) currentSignal->max = temp;
            });

    connect(ui->txtMinVal, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtMinVal->text().toDouble(&result);
                if (result) currentSignal->min = temp;
            });
    connect(ui->txtScale, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                double temp;
                bool result;
                temp = ui->txtScale->text().toDouble(&result);
                if (result) currentSignal->factor = temp;
            });
    connect(ui->txtComment, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                currentSignal->comment = ui->txtComment->text().simplified().replace(' ', '_');
            });

    connect(ui->txtUnitName, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                currentSignal->unitName = ui->txtUnitName->text().simplified().replace(' ', '_');
            });
    connect(ui->txtBitLength, &QLineEdit::textChanged,
            [=]()
            {
                if (currentSignal == nullptr) return;
                int temp;
                temp = Utility::ParseStringToNum(ui->txtBitLength->text());
                if (temp < 0) return;
                if (temp > 63) return;
                if (currentSignal->valType != SP_FLOAT && currentSignal->valType != DP_FLOAT)
                    currentSignal->signalSize = temp;
                fillSignalForm(currentSignal);
            });
    connect(ui->txtName, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                QString tempNameStr = ui->txtName->text().simplified().replace(' ', '_');
                if (tempNameStr.length() > 0) currentSignal->name = tempNameStr;
                //need to update the list too.
                ui->signalsList->currentItem()->setText(currentSignal->name);
            });

    connect(ui->txtMultiplexValue, &QLineEdit::editingFinished,
            [=]()
            {
                if (currentSignal == nullptr) return;
                int temp;
                temp = Utility::ParseStringToNum(ui->txtMultiplexValue->text());
                //TODO: could look up the multiplexor and ensure that the value is within a range that the multiplexor could return
                currentSignal->multiplexValue = temp;
            });
    connect(ui->rbMultiplexed, &QRadioButton::toggled,
            [=](bool state)
            {
                if (state && currentSignal) //signal is now set as a multiplexed signal
                {
                    currentSignal->isMultiplexed = true;
                    currentSignal->isMultiplexor = false;
                    //if the set multiplexor for the message was this signal then clear it
                    if (dbcMessage->multiplexorSignal == currentSignal) dbcMessage->multiplexorSignal = nullptr;
                }
            });

    connect(ui->rbMultiplexor, &QRadioButton::toggled,
            [=](bool state)
            {
                if (state && currentSignal) //signal is now set as a multiplexed signal
                {
                    //don't allow this signal to be a multiplexor if there is already one for this message.
                    //if (dbcMessage->multiplexorSignal != currentSignal && dbcMessage->multiplexorSignal != nullptr) return; //I spoke too soon above...
                    currentSignal->isMultiplexed = false;
                    currentSignal->isMultiplexor = true;
                    //we just set that this is the multiplexor so update the message to show that as well.
                    dbcMessage->multiplexorSignal = currentSignal;
                }
            });

    connect(ui->rbNotMulti, &QRadioButton::toggled,
            [=](bool state)
            {
                if (state && currentSignal) //signal is now set as a multiplexed signal
                {
                    currentSignal->isMultiplexed = false;
                    currentSignal->isMultiplexor = false;
                    if (dbcMessage->multiplexorSignal == currentSignal) dbcMessage->multiplexorSignal = nullptr;
                }
            });

    installEventFilter(this);
}

DBCSignalEditor::~DBCSignalEditor()
{
    removeEventFilter(this);
    delete ui;
}

void DBCSignalEditor::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    writeSettings();
}

bool DBCSignalEditor::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key())
        {
        case Qt::Key_F1:
            HelpWindow::getRef()->showHelp("signaleditor.html");
            break;
        case Qt::Key_F2:
            cloneSignal();
            break;
        }
        return true;
    } else {
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
    return false;
}

void DBCSignalEditor::setFileIdx(int idx)
{
    if (idx < 0 || idx > dbcHandler->getFileCount() - 1) return;
    dbcFile = dbcHandler->getFileByIdx(idx);

    for (int x = 0; x < dbcFile->dbc_nodes.count(); x++)
    {
        ui->comboReceiver->addItem(dbcFile->dbc_nodes[x].name);
    }
}

void DBCSignalEditor::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("DBCSignalEditor/WindowSize", QSize(1000, 600)).toSize());
        move(settings.value("DBCSignalEditor/WindowPos", QPoint(100, 100)).toPoint());
    }
}

void DBCSignalEditor::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("DBCSignalEditor/WindowSize", size());
        settings.setValue("DBCSignalEditor/WindowPos", pos());
    }
}


void DBCSignalEditor::setMessageRef(DBC_MESSAGE *msg)
{
    dbcMessage = msg;
}

void DBCSignalEditor::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    currentSignal = nullptr;
    refreshSignalsList();
}

void DBCSignalEditor::onValuesCellChanged(int row,int col)
{
    if (inhibitCellChanged) return;

    if (row == ui->valuesTable->rowCount() - 1)
    {
        DBC_VAL_ENUM_ENTRY newVal;
        newVal.value = 0;
        newVal.descript = "No Description";
        currentSignal->valList.append(newVal);
        qDebug() << "Created new entry in value list";

        //QTableWidgetItem *widgetVal = new QTableWidgetItem(QString::number(newVal.value));
        //ui->valuesTable->setItem(row, 0, widgetVal);
        //QTableWidgetItem *widgetDesc = new QTableWidgetItem(newVal.descript);
        //ui->valuesTable->setItem(row, 1, widgetDesc);

        //add the blank at the end again
        ui->valuesTable->insertRow(ui->valuesTable->rowCount());
    }

    if (col == 0)
    {
        currentSignal->valList[row].value = Utility::ParseStringToNum(ui->valuesTable->item(row, col)->text());
    }
    else if (col == 1)
    {
        currentSignal->valList[row].descript = ui->valuesTable->item(row, col)->text().simplified().replace(' ', '_');
    }
}

void DBCSignalEditor::onCustomMenuSignals(QPoint point)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(tr("Add a new signal"), this, SLOT(addNewSignal()));
    if (ui->signalsList->currentRow() != -1)
    {
        menu->addAction(tr("Clone currently selected signal"), this, SLOT(cloneSignal()));
        menu->addAction(tr("Delete currently selected signal"), this, SLOT(deleteCurrentSignal()));
    }

    menu->popup(ui->signalsList->mapToGlobal(point));
}

void DBCSignalEditor::onCustomMenuValues(QPoint point)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    menu->addAction(tr("Delete currently selected value"), this, SLOT(deleteCurrentValue()));

    menu->popup(ui->valuesTable->mapToGlobal(point));
}

void DBCSignalEditor::addNewSignal()
{
    int num = qrand() % 70000;
    QString newName = "SIGNAL" + QString::number(num);
    DBC_SIGNAL newSig;
    newSig.name = newName;
    newSig.bias = 0.0;
    newSig.factor = 1.0;
    newSig.intelByteOrder = true;
    newSig.max = 0.0;
    newSig.min = 0.0;
    newSig.receiver = dbcFile->findNodeByIdx(0);
    newSig.signalSize = 1;
    newSig.startBit = 0;
    newSig.valType = UNSIGNED_INT;
    newSig.isMultiplexed = false;
    newSig.isMultiplexor = false;
    newSig.multiplexValue = 0;
    newSig.parentMessage = dbcMessage;

    dbcMessage->sigHandler->addSignal(newSig);

    /* add item at the end of the list and select it */
    /* this will call clickSignalList */
    ui->signalsList->addItem(newName);
    ui->signalsList->setCurrentRow(ui->signalsList->count()-1);
}

void DBCSignalEditor::cloneSignal()
{
    int num = qrand() % 100;

    int idx = ui->signalsList->currentRow();
    if (idx < 0) return;

    DBC_SIGNAL *oldSig = dbcMessage->sigHandler->findSignalByIdx(idx);
    if (!oldSig) return;

    QString newName = oldSig->name + QString::number(num);
    DBC_SIGNAL newSig;
    newSig.name = newName;
    newSig.bias = oldSig->bias;
    newSig.factor = oldSig->factor;
    newSig.intelByteOrder = oldSig->intelByteOrder;
    newSig.max = oldSig->max;
    newSig.min = oldSig->min;
    newSig.receiver = oldSig->receiver;
    newSig.signalSize = oldSig->signalSize;
    newSig.startBit = oldSig->startBit;
    newSig.valType = oldSig->valType;
    newSig.valList.append(oldSig->valList);
    newSig.isMultiplexed = oldSig->isMultiplexed;
    newSig.isMultiplexor = oldSig->isMultiplexor; //maybe should force this false since you can't have two or more!
    newSig.multiplexValue = oldSig->multiplexValue;
    newSig.parentMessage = dbcMessage;

    dbcMessage->sigHandler->addSignal(newSig);

    /* add item at the end of the list */
    ui->signalsList->addItem(newName);

    //unlike adding a signal we don't want to select the new signal here.
    //ui->signalsList->setCurrentRow(ui->signalsList->count()-1);
}

void DBCSignalEditor::deleteCurrentSignal()
{
    int currIdx = ui->signalsList->currentRow();

    //if(currIdx==ui->signalsList->count()-1)
        //return;

    if (currIdx > -1)
    {
        delete(ui->signalsList->item(currIdx));
        dbcMessage->sigHandler->removeSignal(currIdx);
        currentSignal = nullptr;
    }
}

void DBCSignalEditor::deleteCurrentValue()
{
    int currIdx = ui->valuesTable->currentRow();
    if (currIdx > -1)
    {
        ui->valuesTable->removeRow(currIdx);
        currentSignal->valList.removeAt(currIdx);
    }
}

void DBCSignalEditor::refreshSignalsList()
{
    ui->signalsList->clear();


    for (int x = 0; x < dbcMessage->sigHandler->getCount(); x++)
    {
        DBC_SIGNAL *sig = dbcMessage->sigHandler->findSignalByIdx(x);
        ui->signalsList->addItem(sig->name);
    }

    if( ui->signalsList->count()>0 ) {
        /* click first element */
        ui->signalsList->setCurrentRow(0);
    }
    else
        clickSignalList(-1);
}

/* fillSignalForm also handles group "enabled" state */
/* WARNING: fillSignalForm can be called recursively since it is in the listener of cbIntelFormat */
void DBCSignalEditor::fillSignalForm(DBC_SIGNAL *sig)
{
    unsigned char bitpattern[8];

    if (sig == nullptr) {
        ui->groupBox->setEnabled(false);
        ui->txtName->setText("");
        ui->txtBias->setText("");
        ui->txtBitLength->setText("");
        ui->txtComment->setText("");
        ui->txtMaxVal->setText("");
        ui->txtMinVal->setText("");
        ui->txtScale->setText("");
        ui->txtUnitName->setText("");
        ui->txtMultiplexValue->setText("");
        ui->rbMultiplexed->setChecked(false);
        ui->rbMultiplexor->setChecked(false);
        ui->rbNotMulti->setChecked(true);
        memset(bitpattern, 0, 8); //clear it out
        ui->bitfield->setReference(bitpattern, false);
        ui->bitfield->updateData(bitpattern, true);
        ui->comboReceiver->setCurrentIndex(0);
        ui->comboType->setCurrentIndex(0);
        return;
    }

    /* we have a signal */
    ui->groupBox->setEnabled(true);

    generateUsedBits();
    ui->txtName->setText(sig->name);
    ui->txtBias->setText(QString::number(sig->bias));
    ui->txtBitLength->setText(QString::number(sig->signalSize));
    ui->txtMultiplexValue->setText(QString::number(sig->multiplexValue));
    ui->txtComment->setText(sig->comment);
    ui->txtMaxVal->setText(QString::number(sig->max));
    ui->txtMinVal->setText(QString::number(sig->min));
    ui->txtScale->setText(QString::number(sig->factor));
    ui->txtUnitName->setText(sig->unitName);
    ui->rbMultiplexed->setChecked(sig->isMultiplexed);
    ui->rbMultiplexor->setChecked(sig->isMultiplexor);
    ui->rbNotMulti->setChecked( !(sig->isMultiplexor | sig->isMultiplexed) );
    qDebug() << sig->isMultiplexor << "*" << sig->isMultiplexed;

    memset(bitpattern, 0, 8); //clear it out first.

    int startBit, endBit;

    startBit = sig->startBit;

    bitpattern[startBit / 8] |= 1 << (startBit % 8); //make the start bit a different color to set it apart
    ui->bitfield->setReference(bitpattern, false);

    if (sig->intelByteOrder)
    {
        endBit = startBit + sig->signalSize - 1;
        if (startBit < 0) startBit = 0;
        if (endBit > 63) endBit = 63;
        for (int y = startBit; y <= endBit; y++)
        {
            int byt = y / 8;
            bitpattern[byt] |= 1 << (y % 8);
        }
    }
    else //big endian / motorola format
    {
        //much more irritating than the intel version...
        int size = sig->signalSize;
        while (size > 0)
        {
            int byt = startBit / 8;
            bitpattern[byt] |= 1 << (startBit % 8);
            size--;
            if ((startBit % 8) == 0) startBit += 15;            
            else startBit--;
            if (startBit > 63) startBit = 63;
        }
    }

    ui->bitfield->updateData(bitpattern, true);
    ui->cbIntelFormat->setChecked(sig->intelByteOrder);

    switch (sig->valType)
    {
    case UNSIGNED_INT:
        ui->comboType->setCurrentIndex(0);
        break;
    case SIGNED_INT:
        ui->comboType->setCurrentIndex(1);
        break;
    case SP_FLOAT:
        ui->comboType->setCurrentIndex(2);
        break;
    case DP_FLOAT:
        ui->comboType->setCurrentIndex(3);
        break;
    case STRING:
        ui->comboType->setCurrentIndex(4);
        break;
    }

    for (int i = 0; i < ui->comboReceiver->count(); i++)
    {
        if (ui->comboReceiver->itemText(i) == sig->receiver->name)
        {
            ui->comboReceiver->setCurrentIndex(i);
            break;
        }
    }
}

/* fillValueTable also handles "enabled" state */
void DBCSignalEditor::fillValueTable(DBC_SIGNAL *sig)
{
    int rowIdx;

    inhibitCellChanged = true;

    ui->valuesTable->clearContents();
    ui->valuesTable->setRowCount(0);

    if (sig == nullptr) {
        ui->valuesTable->setEnabled(false);
        inhibitCellChanged = false;
        return;
    }

    ui->valuesTable->setEnabled(true);

    for (int i = 0; i < sig->valList.count(); i++)
    {
        QTableWidgetItem *val = new QTableWidgetItem(Utility::formatNumber(sig->valList[i].value));
        QTableWidgetItem *desc = new QTableWidgetItem(sig->valList[i].descript);
        rowIdx = ui->valuesTable->rowCount();
        ui->valuesTable->insertRow(rowIdx);
        ui->valuesTable->setItem(rowIdx, 0, val);
        ui->valuesTable->setItem(rowIdx, 1, desc);
    }

    //Add a blank row at the end to allow for adding records easily
    ui->valuesTable->insertRow(ui->valuesTable->rowCount());

    inhibitCellChanged = false;
}

void DBCSignalEditor::clickSignalList(int row)
{
    //qDebug() << ui->signalsList->item(row)->text();

    DBC_SIGNAL *thisSig = (row<0) ? nullptr : dbcMessage->sigHandler->findSignalByName(ui->signalsList->item(row)->text());
    currentSignal = thisSig;
    fillSignalForm(thisSig);
    fillValueTable(thisSig);
}

void DBCSignalEditor::bitfieldClicked(int x, int y)
{
    int bit = (7 - x) + (y * 8);
    if (currentSignal == nullptr) return;
    currentSignal->startBit = bit;
    if (currentSignal->valType == SP_FLOAT)
    {
        if (currentSignal->startBit > 31) currentSignal->startBit = 39;
    }

    if (currentSignal->valType == DP_FLOAT)
        currentSignal->startBit = 7;
    fillSignalForm(currentSignal);
}

void DBCSignalEditor::generateUsedBits()
{
    uint8_t usedBits[8] = {0,0,0,0,0,0,0,0};
    int startBit, endBit;

    for (int x = 0; x < dbcMessage->sigHandler->getCount(); x++)
    {
        DBC_SIGNAL *sig = dbcMessage->sigHandler->findSignalByIdx(x);

        startBit = sig->startBit;

        if (sig->intelByteOrder)
        {
            endBit = startBit + sig->signalSize - 1;
            if (startBit < 0) startBit = 0;
            if (endBit > 63) endBit = 63;
            for (int y = startBit; y <= endBit; y++)
            {
                int byt = y / 8;
                usedBits[byt] |= 1 << (y % 8);
            }
        }
        else //big endian / motorola format
        {
            //much more irritating than the intel version...
            int size = sig->signalSize;
            while (size > 0)
            {
                int byt = startBit / 8;
                usedBits[byt] |= 1 << (startBit % 8);
                size--;
                if ((startBit % 8) == 0) startBit += 15;
                else startBit--;
                if (startBit > 63) startBit = 63;
            }
        }
    }
    ui->bitfield->setUsed(usedBits, false);
}
