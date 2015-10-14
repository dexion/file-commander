#include "ctextviewerwindow.h"
#include "ctextencodingdetector.h"

DISABLE_COMPILER_WARNINGS
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QStringBuilder>
#include <QTextCodec>
RESTORE_COMPILER_WARNINGS


CTextViewerWindow::CTextViewerWindow() :
	CPluginWindow(),
	_textBrowser(this),
	_findDialog(this, "Plugins/TextViewer/Find/")
{
	setupUi(this);
	setCentralWidget(&_textBrowser);
	_textBrowser.setReadOnly(true);
	_textBrowser.setUndoRedoEnabled(false);
	_textBrowser.setWordWrapMode(QTextOption::NoWrap);

	connect(actionOpen, &QAction::triggered, [this]() {
		const QString fileName = QFileDialog::getOpenFileName(this);
		if (!fileName.isEmpty())
			loadTextFile(fileName);
	});
	connect(actionReload, &QAction::triggered, [this]() {
		loadTextFile(_sourceFilePath);
	});
	connect(actionClose, &QAction::triggered, this, &QDialog::close);

	connect(actionFind, &QAction::triggered, [this]() {
		_findDialog.exec();
	});
	connect(actionFind_next, &QAction::triggered, this, &CTextViewerWindow::findNext);

	connect(actionAuto_detect_encoding, &QAction::triggered, this, &CTextViewerWindow::asDetectedAutomatically);
	connect(actionSystemLocale, &QAction::triggered, this, &CTextViewerWindow::asSystemDefault);
	connect(actionUTF_8, &QAction::triggered, this, &CTextViewerWindow::asUtf8);
	connect(actionUTF_16, &QAction::triggered, this, &CTextViewerWindow::asUtf16);
	connect(actionHTML_RTF, &QAction::triggered, this, &CTextViewerWindow::asRichText);

	QActionGroup * group = new QActionGroup(this);
	group->addAction(actionSystemLocale);
	group->addAction(actionUTF_8);
	group->addAction(actionUTF_16);
	group->addAction(actionHTML_RTF);

	connect(&_findDialog, &CFindDialog::find, this, &CTextViewerWindow::find);
	connect(&_findDialog, &CFindDialog::findNext, this, &CTextViewerWindow::findNext);

	auto escScut = new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
	connect(this, &QObject::destroyed, escScut, &QShortcut::deleteLater);

	_encodingLabel = new QLabel(this);
	QMainWindow::statusBar()->addWidget(_encodingLabel);
}

bool CTextViewerWindow::loadTextFile(const QString& file)
{
	setWindowTitle(file);
	_sourceFilePath = file;

	try
	{
		if (_sourceFilePath.endsWith(".htm", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".html", Qt::CaseInsensitive) || _sourceFilePath.endsWith(".rtf", Qt::CaseInsensitive))
			return asRichText();
		else
			return asDetectedAutomatically();
	}
	catch (const std::bad_alloc&)
	{
		QMessageBox::warning(this, "File is too large", "The text is too large to display");
		return false;
	}
}

bool CTextViewerWindow::asDetectedAutomatically()
{
	QTextCodec::ConverterState state;
	QTextCodec *codec = QTextCodec::codecForName("UTF-8");
	if (!codec)
		return false;

	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(parentWidget(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	QString text(codec->toUnicode(data.constData(), data.size(), &state));
	if (state.invalidChars > 0)
	{
		const auto result = CTextEncodingDetector::decode(data);
		text = result.text;
		if (!text.isEmpty())
		{
			encodingChanged(result.encoding, result.language);
			_textBrowser.setPlainText(text);
		}
		else
			return asSystemDefault();
	}
	else
	{
		encodingChanged("UTF-8");
		actionUTF_8->setChecked(true);
		_textBrowser.setPlainText(text);
	}

	return true;
}

bool CTextViewerWindow::asSystemDefault()
{
	QTextCodec * codec = QTextCodec::codecForLocale();
	if (!codec)
		return false;

	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(parentWidget(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	_textBrowser.setPlainText(codec->toUnicode(data));
	encodingChanged(codec->name());
	actionSystemLocale->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf8()
{
	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(parentWidget(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	encodingChanged("UTF-8");
	_textBrowser.setPlainText(QString::fromUtf8(data));
	actionUTF_8->setChecked(true);

	return true;
}

bool CTextViewerWindow::asUtf16()
{
	QByteArray data;
	if (!readSource(data))
	{
		QMessageBox::warning(parentWidget(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\nIt is inaccessible or doesn't exist.").arg(_sourceFilePath));
		return false;
	}

	encodingChanged("UTF-16");
	_textBrowser.setPlainText(QString::fromUtf16((const ushort*)data.constData()));
	actionUTF_16->setChecked(true);

	return true;
}

bool CTextViewerWindow::asRichText()
{
	// TODO: add RTF support
	return asDetectedAutomatically();
}

void CTextViewerWindow::find()
{
	_textBrowser.moveCursor(_findDialog.searchBackwards() ? QTextCursor::End : QTextCursor::Start);
	findNext();
}

void CTextViewerWindow::findNext()
{
	const QString expression = _findDialog.searchExpression();
	if (expression.isEmpty())
		return;

	QTextDocument::FindFlags flags = 0;
	if (_findDialog.caseSensitive())
		flags |= QTextDocument::FindCaseSensitively;
	if (_findDialog.searchBackwards())
		flags |= QTextDocument::FindBackward;
	if (_findDialog.wholeWords())
		flags |= QTextDocument::FindWholeWords;

	bool found;
	const QTextCursor startCursor = _textBrowser.textCursor();
#if  QT_VERSION >= QT_VERSION_CHECK(5,3,0)
	if (_findDialog.regex())
		found = _textBrowser.find(QRegExp(_findDialog.searchExpression(), _findDialog.caseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive), flags);
	else
#endif
		found = _textBrowser.find(_findDialog.searchExpression(), flags);

	if(!found && (startCursor.isNull() || startCursor.position() == 0))
		QMessageBox::information(this, tr("Not found"), tr("Expression \"%1\" not found").arg(expression));
	else if (!found && startCursor.position() > 0)
	{
		if (QMessageBox::question(this, tr("Not found"), _findDialog.searchBackwards() ? tr("Beginning of file reached, do you want to restart search from the end?") : tr("End of file reached, do you want to restart search from the top?")) == QMessageBox::Yes)
			find();
	}
}

bool CTextViewerWindow::readSource(QByteArray& data) const
{
	QFile file(_sourceFilePath);
	if (file.exists() && file.open(QIODevice::ReadOnly))
	{
		data = file.readAll();
		return data.size() > 0 || file.size() == 0;
	}
	else
		return false;
}

void CTextViewerWindow::encodingChanged(const QString& encoding, const QString& language)
{
	QString message;
	if (!encoding.isEmpty())
		message = tr("Text encoding: ") % encoding;
	if (!language.isEmpty())
		message = message % ", " % tr("language: ") % language;

	_encodingLabel->setText(message);
}
