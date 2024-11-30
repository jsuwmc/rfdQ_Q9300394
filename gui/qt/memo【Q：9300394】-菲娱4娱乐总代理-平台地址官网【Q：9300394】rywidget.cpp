#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "searchwidget.h"
#include "dockwidget.h"
#include "utils.h"
#include "../../core/schedule.h"
#include "../../core/link.h"
#include "../../core/mem.h"

#include <QtGui/QClipboard>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QScrollBar>

#ifdef _MSC_VER
    #include <direct.h>
    #define chdir _chdir
#else
    #include <unistd.h>
#endif

// ------------------------------------------------
// Hex Editor Things
// ------------------------------------------------

QString MainWindow::getAddressString(const QString& string, bool* ok) {
    QString address = QInputDialog::getText(this, tr("Goto"),
                                         tr("Input Address (Or Equate):"), QLineEdit::Normal,
                                         string, ok).toUpper();

    QString exists = getAddressOfEquate(address.toUpper().toStdString());
    if (!exists.isEmpty()) {
        return exists;
    }

   return int2hex(static_cast<uint32_t>(hex2int(address)), 6);
}

void MainWindow::flashUpdate() {
    if (!guiEmuValid) {
        return;
    }

    ui->flashEdit->setData({reinterpret_cast<const char *>(mem.flash.block), 0x400000});
}

void MainWindow::ramUpdate() {
    if (!guiEmuValid) {
        return;
    }

    ui->ramEdit->setBase(0xD00000);
    ui->ramEdit->setData({reinterpret_cast<const char *>(mem.ram.block), 0x65800});
}

void MainWindow::memUpdateEdit(HexWidget *edit, bool force) {
    if (edit == Q_NULLPTR || !guiEmuValid) {
        return;
    }

    QByteArray data;

    if (force || edit->getScrolled() || !edit->getSize()) {
        bool second = edit->getCursorOffset() & 1;
        int off = edit->getOffset();
        int base = edit->getBase();
        int addr = off + base;
        int start = addr - 0x1000;
        int end = addr + 0x1000;
        off = 0x1000;

        if (start < 0) {
            off += start;
            start = 0;
        }
        if (end > 0xFFFFFF) {
            end = 0xFFFFFF;
        }
        data.resize(end - start + 1);

        for (int j = 0, i = start; i < end; j++, i++) {
            data[j] = static_cast<char>(mem_peek_byte(static_cast<uint32_t>(i)));
        }

        edit->setBase(start);
        edit->setData(data);
        if (second) {
            edit->setCursorOffset(off * 2 + 1);
        } else {
            edit->setOffset(off);
        }
    } else {
        int start = edit->getBase();
        int len = edit->getSize();
        data.resize(len);

        for (int j = 0, i = start; j < len; j++, i++) {
            data[j] = static_cast<char>(mem_peek_byte(static_cast<uint32_t>(i)));
        }

        edit->setData(data);
    }
}

void MainWindow::flashGotoPressed() {
    bool accept = false;
    QString addrStr = getAddressString(m_flashGotoAddr, &accept);

    if (accept) {
        m_flashGotoAddr = addrStr;
        ui->flashEdit->setFocus();
        ui->flashEdit->setOffset(hex2int(addrStr));
    }
}

void MainWindow::ramGotoPressed() {
    bool accept = false;
    QString addrStr = getAddressString(m_RamGotoAddr, &accept);

    if (accept) {
        m_RamGotoAddr = addrStr;
        ui->ramEdit->setFocus();
        ui->ramEdit->setOffset(hex2int(addrStr) - 0xD00000);
    }
}

void MainWindow::memSearchEdit(HexWidget *edit) {
    if (edit == Q_NULLPTR) {
        return;
    }

    SearchWidget search(m_searchStr, m_searchMode);
    int searchMode, found = 0;
    search.show();

    if ((searchMode = search.exec()) == SearchWidget::Cancel) {
        return;
    }

    m_searchMode = search.getType();
    m_searchStr = search.getSearchString();

    QString searchString;
    if (m_searchMode == SearchWidget::Hex) {
        searchString = m_searchStr;
    } else {
        searchString = QString::fromStdString(m_searchStr.toLatin1().toHex().toStdString());
    }

    edit->setFocus();
    std::string s = searchString.toUpper().toStdString();
    if (searchString.isEmpty() || (searchString.length() & 1) || s.find_first_not_of("0123456789ABCDEF") != std::string::npos) {
        QMessageBox::critical(this, MSG_ERROR, tr("Error when reading input string"));
        return;
    }

    QByteArray searchBa = QByteArray::fromHex(searchString.toLatin1());

    switch (searchMode) {
        default:
        case SearchWidget::NextNot:
            found = edit->indexNotOf(searchBa);
            break;
        case SearchWidget::Prev:
            found = edit->indexPrevOf(searchBa);
            break;
        case SearchWidget::PrevNot:
            found = edit->indexPrevNotOf(searchBa);
            break;
        case SearchWidget::Next:
            found = edit->indexOf(searchBa);
            break;
    }

    if (found == -1) {
         QMessageBox::warning(this, MSG_WARNING, tr("String not found."));
    }
}

void MainWindow::memGoto(HexWidget *edit, uint32_t address) {
    if (edit == Q_NULLPTR) {
        return;
    }

    edit->setBase(static_cast<int>(address));
    edit->setOffset(0);
    memUpdateEdit(edit, true);
}

void MainWindow::memGotoEdit(HexWidget *edit) {
    if (edit == Q_NULLPTR) {
        return;
    }

    bool accept = false;
    QString address = getAddressString(m_memGotoAddr, &accept);

    if (accept) {
        memGoto(edit, static_cast<uint32_t>(hex2int(m_memGotoAddr = address)));
    }
}

void MainWindow::memSync(HexWidget *edit) {
    if (edit == Q_NULLPTR) {
        return;
    }

    debugPopulate();
    disasmUpdateAddr(m_disasmAddr, m_disasmPane);
    edit->setFocus();
}

void MainWindow::flashSyncPressed() {
    if (ui->flashEdit->modifiedCount()) {
        memcpy(mem.flash.block, ui->flashEdit->data(), 0x400000);
    }
    memSync(ui->flashEdit);
}

void MainWindow::ramSyncPressed() {
    if (ui->ramEdit->modifiedCount()) {
        memcpy(mem.ram.block, ui->ramEdit->data(), 0x65800);
    }
    memSync(ui->ramEdit);
}

void MainWindow::memSyncEdit(HexWidget *edit) {
    if (edit == Q_NULLPTR) {
        return;
    }

    int base = edit->getBase();
    int count = edit->modifiedCount();
    for (int i = 0; count && i < edit->getSize(); i++) {
        if (edit->modified()[i]) {
            mem_poke_byte(static_cast<uint32_t>(base + i), edit->data()[i]);
            count--;
        }
        qApp->processEvents();
    }

    memSync(edit);
}

void MainWindow::memAsciiToggle(HexWidget *edit) {
    if (edit == Q_NULLPTR) {
        return;
    }

    edit->setAsciiArea(!edit->getAsciiArea());
}

void MainWindow::contextMem(const QPoint &posa) {
    HexWidget *p = qobject_cast<HexWidget*>(sender());
    contextMemWidget(p->mapToGlobal(posa), static_cast<uint32_t>(p->getOffset() + p->getBase()));
}

void MainWindow::contextMemWidget(const QPoint &pos, uint32_t address) {
    QString copyAddr = tr("Copy Address");
    QString toggleBreak = tr("Toggle Breakpoint");
    QString toggleWrite = tr("Toggle Write Watchpoint");
    QString toggleRead = tr("Toggle Read Watchpoint");
    QString toggleReadWrite = tr("Toggle Read/Write Watchpoint");
    QString addr = int2hex(address, 6);

    copyAddr += QStringLiteral(" '") + addr + QStringLiteral("'");

    QMenu menu;
    menu.addAction(copyAddr);
    menu.addSeparator();
    menu.addAction(toggleBreak);
    menu.addAction(toggleRead);
    menu.addAction(toggleWrite);
    menu.addAction(toggleReadWrite);

    QAction* item = menu.exec(pos);
    if (item) {
        if (item->text() == copyAddr) {
            qApp->clipboard()->setText(addr.toLatin1());
        } else if (item->text() == toggleBreak) {
            breakAdd(breakNextLabel(), address, true, true, false);
            memDocksUpdate();
        } else if (item->text() == toggleRead) {
            watchAdd(watchNextLabel(), address, address, DBG_MASK_READ, true, false);
            memDocksUpdate();
        } else if (item->text() == toggleWrite) {
            watchAdd(watchNextLabel(), address, address, DBG_MASK_WRITE, true, false);
            memDocksUpdate();
        } else if (item->text() == toggleReadWrite) {
            watchAdd(watchNextLabel(), address, address, DBG_MASK_READ | DBG_MASK_WRITE, true, false);
            memDocksUpdate();
        }
    }
}
