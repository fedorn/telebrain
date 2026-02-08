/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#include "ai/ai_chat_widget.h"
#include "ai/ai_contstants.h"

#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "history/view/controls/history_view_compose_controls.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "base/unixtime.h"
#include "menu/menu_send.h"
#include "styles/style_window.h"
#include "styles/style_chat.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/message_bubble.h"
#include "ui/text/text_utilities.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "base/timer.h"
#include "history/view/history_view_element.h"
#include <QtGui/QPainterPath>
#include <QtGui/QTextLayout>
#include <QtGui/QClipboard>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QObject>

namespace AI {

// AI Chat background color
const QColor kAIChatBackgroundColor(241, 241, 241);
class MessagesWidget : public Ui::RpWidget {
public:
	MessagesWidget(
		QWidget *parent,
		not_null<Ui::ChatStyle*> chatStyle,
		const std::vector<MessageData> &messages)
	: RpWidget(parent)
	, _chatStyle(chatStyle)
	, _messages(messages) {
		setAttribute(Qt::WA_OpaquePaintEvent);
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		
		updateSize();
	}

	void setMessages(const std::vector<MessageData> &messages) {
		_messages = messages;
		clearTextSelection();
		clearMessageSelection();
		updateSize();
		update();
	}

	void resizeToWidth(int width) {
		if (this->width() != width) {
			resize(width, height());
			updateSize();
		}
	}

	void clearTextSelection() {
		_selectedMessageIndex = -1;
		_textSelection = TextSelection();
		// Only reset _mouseAction if we're not in message selection mode
		if (!_inMessageSelectionMode) {
			_mouseAction = MouseAction::None;
		}
		// Don't reset _mouseSelectType here to preserve triple-click state
		// Don't reset _inMessageSelectionMode here, it should be reset explicitly
		// Don't reset click counter here to preserve triple-click detection
		setCursor(Qt::ArrowCursor);
		update();
	}

	void clearMessageSelection() {
		if (!_selectedMessages.empty()) {
			_selectedMessages.clear();
			_inMessageSelectionMode = false;
			update();
		}
		// Note: This method doesn't cancel the triple-click timer
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		// Use a custom light blue background for AI chat
		p.fillRect(rect(), kAIChatBackgroundColor);

		const auto messagesRect = rect().marginsRemoved(QMargins(20, 20, 20, 20));
		const auto spacing = 8;
		auto y = messagesRect.top();

		// Get message styling
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;

		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();
		const auto maxBubbleWidth = messagesRect.width() - msgMargin.left() - msgMargin.right();
		for (size_t i = 0; i < _messages.size(); ++i) {
			const auto &message = _messages[i];
			
			// Calculate text size using same engine as drawing (fixes emoji/last-line overflow)
			const auto textSize = getTextSizeForBubble(message.text, textAvailableWidth);
			const auto bubbleWidth = std::min(
				textSize.width() + msgPadding.left() + msgPadding.right(),
				maxBubbleWidth);
			const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();
			
			// Position bubble (user messages on right, AI messages on left)
			int bubbleX;
			if (message.isFromUser) {
				// User message on the right
				bubbleX = messagesRect.right() - bubbleWidth;
			} else {
				// AI message on the left
				bubbleX = messagesRect.left();
			}
			
			const auto bubbleRect = QRect(bubbleX, y, bubbleWidth, bubbleHeight);

			// Check if this message is selected
			const auto isSelected = _selectedMessages.find(static_cast<int>(i)) != _selectedMessages.end();

			// Use proper bubble painting with ChatStyle
			Ui::PaintBubble(p, Ui::SimpleBubble{
				.st = _chatStyle.get(),
				.geometry = bubbleRect,
				.outerWidth = width(),
				.selected = isSelected,
				.shadowed = true,
				.outbg = message.isFromUser,
				.rounding = Ui::BubbleRounding{
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
					Ui::BubbleCornerRounding::Large,
				},
			});

			// Draw message text with selection highlighting
			const auto &messageStyle = _chatStyle->messageStyle(message.isFromUser, isSelected);
			const auto textRect = bubbleRect.marginsRemoved(msgPadding);
			
			// Check if this message has text selection
			const auto hasSelection = (_selectedMessageIndex == i) && !_textSelection.empty();
			
			if (hasSelection) {
				// Draw text with selection highlighting
				drawTextWithSelection(p, textRect, message.text, _textSelection, messageStyle);
			} else {
				// Draw normal text using the same approach for consistency
				drawTextWithSelection(p, textRect, message.text, TextSelection(), messageStyle);
			}

			y += bubbleHeight + spacing;
		}
	}

	void mousePressEvent(QMouseEvent *e) override {
		qDebug() << "MessagesWidget::mousePressEvent - button:" << e->button() << "pos:" << e->pos();
		if (e->button() == Qt::LeftButton) {
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				const auto &message = _messages[messageIndex];
				const auto textRect = getMessageTextRect(messageIndex);
				const auto localPos = e->pos() - textRect.topLeft();

				// --- Triple-click detection ---
				const qint64 now = QDateTime::currentMSecsSinceEpoch();
				const int interval = QApplication::doubleClickInterval();
				
				if (_clickCount > 0 && (now - _lastClickTime) < interval) {
					if (_clickCount < 3) {
						++_clickCount;
					}
				} else {
					_clickCount = 1;
				}
				_lastClickTime = now;

				const auto textPos = getTextPositionAtPoint(textRect, localPos, message.text);
				clearMessageSelection();
				_selectedMessageIndex = messageIndex;
				_mouseAction = MouseAction::Selecting;
				_mouseTextSymbol = static_cast<uint16>(textPos);
				if (_clickCount == 3) {
					// Triple-click: select paragraph
					auto selection = TextSelection(textPos, textPos); 
					_textSelection = adjustSelectionToParagraphs(message.text, selection);
					_mouseSelectType = TextSelectType::Paragraphs;
				} else {
					// Single click: start letter selection	
					_textSelection = TextSelection(textPos, textPos);
					_mouseSelectType = TextSelectType::Letters; // Reset to letters for new selection
					_pressMessageIndex = messageIndex;
					_inMessageSelectionMode = false; // Ensure we start in text selection mode
				}
				setFocus();
				setCursorForTextSelection();
				update();
			}
			else {
				clearTextSelection();
				clearMessageSelection();
			}
		}
		else {
			clearTextSelection();
			clearMessageSelection();
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		if (_mouseAction == MouseAction::Selecting) {
			if (_inMessageSelectionMode) {
				// We're in message selection mode, update the selection range
				const auto mouseY = e->pos().y();
				const auto currentMessageIndex = findMessageAtY(mouseY);
				if (currentMessageIndex >= 0) {
					updateMessageSelection(_pressMessageIndex, currentMessageIndex);
				}
				setCursor(Qt::ArrowCursor);
				update();
			} else if (_selectedMessageIndex >= 0) {
				// We're in text selection mode
				const auto textRect = getMessageTextRect(_selectedMessageIndex);
				const auto localPos = e->pos() - textRect.topLeft();
				
				// Check if we're still within the same message's text area
				const auto isWithinTextArea = textRect.contains(e->pos());
				
				if (isWithinTextArea) {
					// Continue text selection within the same message
					const auto &message = _messages[_selectedMessageIndex];
					const auto textPos = getTextPositionAtPoint(textRect, localPos, message.text);
					
					// Update selection based on selection type
					auto selection = TextSelection(
						std::min(_mouseTextSymbol, static_cast<uint16>(textPos)),
						std::max(_mouseTextSymbol, static_cast<uint16>(textPos))
					);
					
					// Adjust selection based on type (words, paragraphs, etc.)
					if (_mouseSelectType == TextSelectType::Words) {
						selection = adjustSelectionToWords(message.text, selection);
					} else if (_mouseSelectType == TextSelectType::Paragraphs) {
						selection = adjustSelectionToParagraphs(message.text, selection);
					}
					// For TextSelectType::Letters, use the selection as-is (no adjustment)
					
					_textSelection = selection;
					clearMessageSelection();
					setCursorForTextSelection();
					update();
				} else {
					// Switch to message selection mode
					_inMessageSelectionMode = true; // Enter message selection mode first
					_mouseSelectType = TextSelectType::Letters; // Reset paragraph selection mode when switching modes
					clearTextSelection(); // Now clear text selection (won't reset _mouseAction)
					_selectedMessageIndex = -1; // Clear text selection state
					// Keep _mouseAction as Selecting to continue processing mouse moves
					
					// Find the message at the current Y position, even if outside message bounds
					const auto currentMessageIndex = findMessageAtY(e->pos().y());
					if (currentMessageIndex >= 0) {
						updateMessageSelection(_pressMessageIndex, currentMessageIndex);
					}
					setCursor(Qt::ArrowCursor);
					update();
				}
			}
		} else {
			// Check if mouse is over a message to show text cursor
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				setCursor(Qt::IBeamCursor);
			} else {
				setCursor(Qt::ArrowCursor);
			}
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		qDebug() << "MessagesWidget::mouseReleaseEvent - button:" << e->button() << "pos:" << e->pos() << "mouseAction:" << static_cast<int>(_mouseAction);
		if (e->button() == Qt::LeftButton && _mouseAction == MouseAction::Selecting) {
			// Only clear selection if it's empty and we're not in a special selection mode
			if (_textSelection.from == _textSelection.to && _mouseSelectType == TextSelectType::Letters) {
				clearTextSelection();
			}
			
			// Reset mouse action to end selection mode
			_mouseAction = MouseAction::None;
			
			// Don't reset selection type here - let it persist for continued selection modes
			// Only reset to letters if we're not in a special selection mode
			if (_mouseSelectType != TextSelectType::Paragraphs) {
				_mouseSelectType = TextSelectType::Letters;
			}
			
			// Don't reset click counter here - let it persist for potential triple-click
			
			_inMessageSelectionMode = false; // Reset message selection mode
			
			// Copy to selection clipboard if supported
			if (QApplication::clipboard()->supportsSelection()) {
				if (!_textSelection.empty()) {
					const auto selectedText = getSelectedText();
					if (!selectedText.isEmpty()) {
						QApplication::clipboard()->setText(selectedText, QClipboard::Selection);
					}
				} else if (!_selectedMessages.empty()) {
					const auto selectedText = getSelectedMessagesText();
					if (!selectedText.isEmpty()) {
						QApplication::clipboard()->setText(selectedText, QClipboard::Selection);
					}
				}
			}
		} else if (e->button() == Qt::LeftButton) {
			// Left button release but not in selecting mode - don't reset paragraph selection mode here
			// Let it persist for continued paragraph selection
		}
	}

	void mouseDoubleClickEvent(QMouseEvent *e) override {
		qDebug() << "MessagesWidget::mouseDoubleClickEvent - button:" << e->button() << "pos:" << e->pos();
		if (e->button() == Qt::LeftButton) {
			const auto messageIndex = findMessageAtPoint(e->pos());
			if (messageIndex >= 0) {
				const auto &message = _messages[messageIndex];
				const auto textRect = getMessageTextRect(messageIndex);
				const auto localPos = e->pos() - textRect.topLeft();
				
				// Set click counter to 2 for double-click to enable triple-click detection
				// But don't reset it if we're already in paragraph selection mode (clickCount == 3)
				if (_clickCount != 3) {
					_clickCount = 2;
				}
				
				// Clear any existing selections first
				clearMessageSelection();
				
				// Calculate text position using Ui::Text system
				const auto textPos = getTextPositionAtPoint(textRect, localPos, message.text);
				
				// Set up selection state
				_selectedMessageIndex = messageIndex;
				_mouseTextSymbol = static_cast<uint16>(textPos);
				_mouseAction = MouseAction::Selecting; // Set mouse action for proper state management
				_inMessageSelectionMode = false; // Ensure we're in text selection mode
				
				qDebug() << "MouseSelectType:" << (_mouseSelectType == TextSelectType::Letters ? "Letters" : (_mouseSelectType == TextSelectType::Words ? "Words" : "Paragraphs"));
				// If we're already in paragraph selection mode, preserve existing selection
				if (_mouseSelectType == TextSelectType::Paragraphs) {
					// Select paragraph at position using the paragraph selection logic
					auto selection = TextSelection(textPos, textPos); // Empty selection at position
					_textSelection = adjustSelectionToParagraphs(message.text, selection);
				} else {
					// Set to word selection mode and select word at position
					_mouseSelectType = TextSelectType::Words;
					auto selection = TextSelection(textPos, textPos); // Empty selection at position
					_textSelection = adjustSelectionToWords(message.text, selection);
				}
				
				// Ensure immediate visual feedback
				setFocus();
				setCursorForTextSelection();
				update();
				
				// Force an immediate repaint to show the selection
				repaint();
			} else {
				// Double click outside messages - reset to letter selection mode
				_mouseSelectType = TextSelectType::Letters;
			}
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		if (e == QKeySequence::Copy) {
			QString textToCopy;
			if (!_textSelection.empty()) {
				textToCopy = getSelectedText();
			} else if (!_selectedMessages.empty()) {
				textToCopy = getSelectedMessagesText();
			}
			
			if (!textToCopy.isEmpty()) {
				QApplication::clipboard()->setText(textToCopy);
			}
			e->accept();
		} else if (e->key() == Qt::Key_Escape) {
			clearTextSelection();
			clearMessageSelection();
			// Reset selection type to letters when pressing Escape
			_mouseSelectType = TextSelectType::Letters;
			e->accept();
		} else {
			RpWidget::keyPressEvent(e);
		}
	}

	void focusOutEvent(QFocusEvent *e) override {
		clearTextSelection();
		clearMessageSelection();
		// Reset selection type to letters when losing focus
		_mouseSelectType = TextSelectType::Letters;
		RpWidget::focusOutEvent(e);
	}

	void resizeEvent(QResizeEvent *e) override {
		RpWidget::resizeEvent(e);
		updateSize();
	}

private:
	enum class MouseAction {
		None,
		Selecting,
	};

	void updateSize() {
		const auto availableWidth = width() > 0 ? width() : 400; // Default width if not set yet
		const auto messagesRect = QRect(0, 0, availableWidth, 1000).marginsRemoved(QMargins(20, 20, 20, 20));
		const auto spacing = 8;
		auto y = messagesRect.top();

		// Get message styling
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;

		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();
		for (const auto &message : _messages) {
			// Calculate text size using same engine as drawing (fixes emoji/last-line overflow)
			const auto textSize = getTextSizeForBubble(message.text, textAvailableWidth);
			const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();
			
			y += bubbleHeight + spacing;
		}

		// Calculate exact height needed - remove the last spacing if there are messages
		const auto totalHeight = _messages.empty() ? 0 : (y - messagesRect.top() - spacing + 40); // Add 40px bottom margin for space above compose controls
		
		// Set the exact height needed
		if (width() > 0) {
			resize(width(), totalHeight);
		} else {
			resize(availableWidth, totalHeight);
		}
	}

	void updateMessageSelection(int fromIndex, int toIndex) {
		if (fromIndex < 0 || toIndex < 0) {
			clearMessageSelection();
			return;
		}

		// Ensure fromIndex <= toIndex
		if (fromIndex > toIndex) {
			std::swap(fromIndex, toIndex);
		}

		// Clear existing selection and add new range
		_selectedMessages.clear();
		for (int i = fromIndex; i <= toIndex; ++i) {
			if (i >= 0 && i < static_cast<int>(_messages.size())) {
				_selectedMessages.insert(i);
			}
		}
	}

	QString getSelectedMessagesText() const {
		if (_selectedMessages.empty()) {
			return QString();
		}

		QStringList texts;
		for (const auto index : _selectedMessages) {
			if (index >= 0 && index < static_cast<int>(_messages.size())) {
				texts.append(_messages[index].text);
			}
		}
		return texts.join(u"\n\n"_q);
	}

	int findMessageAtPoint(const QPoint &point) const {
		const auto messagesRect = rect().marginsRemoved(QMargins(20, 20, 20, 20));
		const auto spacing = 8;
		auto y = messagesRect.top();

		// Get message styling
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;

		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();
		const auto maxBubbleWidth = messagesRect.width() - msgMargin.left() - msgMargin.right();
		for (size_t i = 0; i < _messages.size(); ++i) {
			const auto &message = _messages[i];
			
			// Calculate text size using same engine as drawing (fixes emoji/last-line overflow)
			const auto textSize = getTextSizeForBubble(message.text, textAvailableWidth);
			const auto bubbleWidth = std::min(
				textSize.width() + msgPadding.left() + msgPadding.right(),
				maxBubbleWidth);
			const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();
			
			// Position bubble
			int bubbleX;
			if (message.isFromUser) {
				bubbleX = messagesRect.right() - bubbleWidth;
			} else {
				bubbleX = messagesRect.left();
			}

			const auto bubbleRect = QRect(bubbleX, y, bubbleWidth, bubbleHeight);
			
			if (bubbleRect.contains(point)) {
				return static_cast<int>(i);
			}
			
			y += bubbleHeight + spacing;
		}
		
		return -1;
	}

	int findMessageAtY(int y) const {
		const auto messagesRect = rect().marginsRemoved(QMargins(20, 20, 20, 20));
		const auto spacing = 8;
		auto currentY = messagesRect.top();

		// Get message styling
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;
		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();

		for (size_t i = 0; i < _messages.size(); ++i) {
			const auto &message = _messages[i];
			
			// Calculate text size using same engine as drawing (fixes emoji/last-line overflow)
			const auto textSize = getTextSizeForBubble(message.text, textAvailableWidth);
			const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();
			
			// Check if the Y coordinate falls within this message's vertical range
			if (y >= currentY && y < currentY + bubbleHeight) {
				return static_cast<int>(i);
			}
			
			currentY += bubbleHeight + spacing;
		}
		
		// If Y is above the first message, return the first message
		if (y < messagesRect.top() && !_messages.empty()) {
			return 0;
		}
		
		// If Y is below the last message, return the last message
		if (y >= currentY && !_messages.empty()) {
			return static_cast<int>(_messages.size() - 1);
		}
		
		return -1;
	}

	QRect getMessageTextRect(int messageIndex) const {
		if (messageIndex < 0 || messageIndex >= static_cast<int>(_messages.size())) {
			return QRect();
		}

		const auto messagesRect = rect().marginsRemoved(QMargins(20, 20, 20, 20));
		const auto spacing = 8;
		auto y = messagesRect.top();

		// Get message styling
		const auto msgPadding = st::msgPadding;
		const auto msgMargin = st::msgMargin;

		const auto textAvailableWidth = messagesRect.width() - msgMargin.left() - msgMargin.right() - msgPadding.left() - msgPadding.right();
		const auto maxBubbleWidth = messagesRect.width() - msgMargin.left() - msgMargin.right();
		for (int i = 0; i <= messageIndex; ++i) {
			const auto &message = _messages[i];
			
			// Calculate text size using same engine as drawing (fixes emoji/last-line overflow)
			const auto textSize = getTextSizeForBubble(message.text, textAvailableWidth);
			const auto bubbleWidth = std::min(
				textSize.width() + msgPadding.left() + msgPadding.right(),
				maxBubbleWidth);
			const auto bubbleHeight = textSize.height() + msgPadding.top() + msgPadding.bottom();
			
			// Position bubble
			int bubbleX;
			if (message.isFromUser) {
				bubbleX = messagesRect.right() - bubbleWidth;
			} else {
				bubbleX = messagesRect.left();
			}
			
			const auto bubbleRect = QRect(bubbleX, y, bubbleWidth, bubbleHeight);
			
			if (i == messageIndex) {
				return bubbleRect.marginsRemoved(msgPadding);
			}
			
			y += bubbleHeight + spacing;
		}
		
		return QRect();
	}

	int getTextPositionAtPoint(const QRect &textRect, const QPoint &point, const QString &text) const {
		if (text.isEmpty()) {
			return 0;
		}
		
		// Use same bounded width as sizing/drawing so hit-test matches layout
		Ui::Text::String textString(st::messageTextStyle, text, kDefaultTextOptions, textRect.width());
		
		// Use the same approach as Telegram's history view
		Ui::Text::StateRequest request;
		request.flags = Ui::Text::StateRequest::Flag::LookupSymbol;
		
		const auto state = textString.getState(point, textRect.width(), request);
		
		// Handle the afterSymbol flag like Telegram does
		auto symbol = state.symbol;
		if (state.afterSymbol && symbol < text.length()) {
			++symbol;
		}
		
		return symbol;
	}

	TextSelection adjustSelectionToWords(const QString &text, const TextSelection &selection) const {
		if (text.isEmpty()) {
			return TextSelection();
		}
		
		// If selection is empty, find the word at the position
		if (selection.empty()) {
			// Use the from position as the cursor position
			const auto position = selection.from;
			if (position > text.length()) {
				return TextSelection();
			}
			
			auto start = position;
			auto end = position;
			
			// Expand start to word boundary using Telegram's word separator logic
			while (start > 0 && !Ui::Text::IsWordSeparator(text[start - 1])) {
				--start;
			}
			
			// Expand end to word boundary using Telegram's word separator logic
			while (end < text.length() && !Ui::Text::IsWordSeparator(text[end])) {
				++end;
			}
			
			return TextSelection(start, end);
		}
		
		auto start = selection.from;
		auto end = selection.to;
		
		// Expand start to word boundary using Telegram's word separator logic
		while (start > 0 && !Ui::Text::IsWordSeparator(text[start - 1])) {
			--start;
		}
		
		// Expand end to word boundary using Telegram's word separator logic
		while (end < text.length() && !Ui::Text::IsWordSeparator(text[end])) {
			++end;
		}
		
		return TextSelection(start, end);
	}

	TextSelection adjustSelectionToParagraphs(const QString &text, const TextSelection &selection) const {
		if (text.isEmpty()) {
			return TextSelection();
		}
		
		// If selection is empty, find the paragraph at the position
		if (selection.empty()) {
			// Use the from position as the cursor position
			const auto position = selection.from;
			if (position > text.length()) {
				return TextSelection();
			}
			
			auto start = position;
			auto end = position;
			
			// Expand start to paragraph boundary
			while (start > 0) {
				if (start >= 2 && text[start - 1] == QChar::LineFeed && text[start - 2] == QChar::LineFeed) {
					break; // Found double newline
				}
				--start;
			}
			
			// Expand end to paragraph boundary
			while (end < text.length()) {
				if (end + 1 < text.length() && text[end] == QChar::LineFeed && text[end + 1] == QChar::LineFeed) {
					break; // Found double newline
				}
				++end;
			}
			
			// Ensure we have a valid selection
			if (start >= end) {
				// If no paragraph boundaries found, select the entire text
				return TextSelection(0, text.length());
			}
			
			return TextSelection(start, end);
		}
		
		auto start = selection.from;
		auto end = selection.to;
		
		// Expand start to paragraph boundary
		while (start > 0) {
			if (start >= 2 && text[start - 1] == QChar::LineFeed && text[start - 2] == QChar::LineFeed) {
				break; // Found double newline
			}
			--start;
		}
		
		// Expand end to paragraph boundary
		while (end < text.length()) {
			if (end + 1 < text.length() && text[end] == QChar::LineFeed && text[end + 1] == QChar::LineFeed) {
				break; // Found double newline
			}
			++end;
		}
		
		// Ensure we have a valid selection
		if (start >= end) {
			// If no paragraph boundaries found, select the entire text
			return TextSelection(0, text.length());
		}
		
		return TextSelection(start, end);
	}

	// Use Ui::Text::String for sizing so bubble dimensions match actual drawing (fixes emoji/last-line overflow).
	// Pass availableWidth as minResizeWidth so the string wraps; default kQFixedMax would make layout use unbounded width.
	static QSize getTextSizeForBubble(const QString &text, int availableWidth) {
		if (text.isEmpty()) {
			return QSize(0, 0);
		}
		Ui::Text::String textString(st::messageTextStyle, text, kDefaultTextOptions, availableWidth);
		const auto w = textString.countWidth(availableWidth);
		const auto h = textString.countHeight(availableWidth);
		return QSize(w, h);
	}

	void drawTextWithSelection(Painter &p, const QRect &rect, const QString &text, const TextSelection &selection, const Ui::MessageStyle &style) {
		// Create a Ui::Text::String with rect.width() so layout wraps (same as getTextSizeForBubble).
		Ui::Text::String textString(st::messageTextStyle, text, kDefaultTextOptions, rect.width());
		
		// Set the text color for the painter
		p.setPen(style.historyTextFg);
		p.setFont(st::msgFont);
		
		// Clip to text rect so the last line (e.g. with emojis) never overflows the bubble
		p.save();
		p.setClipRect(rect);
		// Draw the text with selection using the same approach as Telegram's history view
		textString.draw(p, {
			.position = rect.topLeft(),
			.availableWidth = rect.width(),
			.palette = &style.textPalette,
			.selection = selection,
			.useFullWidth = true,
		});
		p.restore();
	}

	QString getSelectedText() const {
		if (_selectedMessageIndex < 0 || _selectedMessageIndex >= static_cast<int>(_messages.size()) || _textSelection.empty()) {
			return QString();
		}
		
		const auto &message = _messages[_selectedMessageIndex];
		const auto from = qBound(0, static_cast<int>(_textSelection.from), static_cast<int>(message.text.length()));
		const auto to = qBound(0, static_cast<int>(_textSelection.to), static_cast<int>(message.text.length()));
		
		return message.text.mid(from, to - from);
	}

	void setCursorForTextSelection() {
		if (_selectedMessageIndex >= 0 && !_textSelection.empty()) {
			setCursor(Qt::IBeamCursor);
		} else {
			setCursor(Qt::ArrowCursor);
		}
	}

	not_null<Ui::ChatStyle*> _chatStyle;
	std::vector<MessageData> _messages;
	
	// Text selection state
	int _selectedMessageIndex = -1;
	TextSelection _textSelection;
	MouseAction _mouseAction = MouseAction::None;
	uint16 _mouseTextSymbol = 0;
	TextSelectType _mouseSelectType = TextSelectType::Letters;
	bool _inMessageSelectionMode = false;
	
	// Triple-click detection state
	int _clickCount = 0;
	qint64 _lastClickTime = 0;

	int _pressMessageIndex;

	std::set<int> _selectedMessages;
};

AIChatWidget::AIChatWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Window::SectionWidget(parent, controller) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	// Create chat style for proper message bubble styling
	_chatStyle = std::make_unique<Ui::ChatStyle>(style::main_palette::get());

	// Create OpenAI client
	_openaiClient = std::make_unique<OpenAIClient>(this);
	_openaiClient->setSessionController(controller);

	// Create clear button
	_clearButton = std::make_unique<Ui::RoundButton>(
		this,
		rpl::single<QString>("Clear"),
		st::defaultActiveButton);
	_clearButton->setClickedCallback([=] { clearChat(); });

	// Create scroll area for messages
	_scroll = std::make_unique<Ui::ScrollArea>(
		this,
		st::historyScroll,
		false);

	// Create messages widget
	_messagesWidget = std::make_unique<MessagesWidget>(
		_scroll.get(),
		_chatStyle.get(),
		_messages);
	
	// Set the messages widget as the scroll area's content
	_scroll->setOwnedWidget(object_ptr<MessagesWidget>::fromRaw(_messagesWidget.get()));

	// Create compose controls
	setupComposeControls();

	// Set up layout
	_clearButton->show();
	_scroll->show();
	_composeControls->show();

	// Handle compose controls height changes
	_composeControls->height(
	) | rpl::on_next([=] {
		updateControlsGeometry();
	}, lifetime());

	// Handle send requests
	_composeControls->sendRequests(
	) | rpl::on_next([=](Api::SendOptions options) {
		const auto text = _composeControls->getTextWithAppliedMarkdown().text;
		if (!text.isEmpty()) {
			handleSendMessage(text);
		}
	}, lifetime());

	// Add welcome message
	addAIMessage(kWelcomeMessage);
}

void AIChatWidget::setupComposeControls() {
	// Create a simple history for the compose controls
	auto history = controller()->session().data().history(
		controller()->session().userPeerId());

	_composeControls = std::make_unique<HistoryView::ComposeControls>(
		this,
		HistoryView::ComposeControlsDescriptor{
			.show = controller()->uiShow(),
			.unavailableEmojiPasted = [=](not_null<DocumentData*> emoji) {
				// Handle emoji paste
			},
			.mode = HistoryView::ComposeControlsMode::Normal,
			.sendMenuDetails = [=] { 
				return SendMenu::Details{ .type = SendMenu::Type::SilentOnly };
			},
			.regularWindow = controller(),
			.stickerOrEmojiChosen = controller()->stickerOrEmojiChosen(),
			.features = {
				.likes = false,
				.sendAs = false,
				.ttlInfo = false,
				.botCommandSend = false,
				.silentBroadcastToggle = false,
				.attachBotsMenu = false,
				.inlineBots = false,
				.megagroupSet = false,
				.collectibleStatus = false,
				.stickersSettings = false,
				.openStickerSets = false,
				.autocompleteHashtags = false,
				.autocompleteMentions = false,
				.autocompleteCommands = false,
				.suggestStickersByEmoji = false,
				.commonTabbedPanel = false,
			},
		});

	// Set up the history for compose controls
	_composeControls->setHistory({
		.history = history.get(),
		.topicRootId = MsgId(),
		.monoforumPeerId = PeerId(),
		.showSlowmodeError = [=] { return false; },
		.sendActionFactory = [=] { 
			return Api::SendAction(history, Api::SendOptions());
		},
		.slowmodeSecondsLeft = rpl::single(0),
		.sendDisabledBySlowmode = rpl::single(false),
		.writeRestriction = rpl::single(HistoryView::Controls::WriteRestriction()),
	});

	_composeControls->setAIChatMode();
}

void AIChatWidget::handleSendMessage(const QString &text) {
	if (text.isEmpty()) {
		return;
	}

	// Add user message
	addUserMessage(text);

	// Clear the input field
	_composeControls->clear();

	// Send to OpenAI API
	sendToOpenAI();
}

void AIChatWidget::addUserMessage(const QString &text) {
	// Create a simple message entry
	_messages.push_back({
		.text = text,
		.isFromUser = true,
		.timestamp = base::unixtime::now()
	});

	// Update the messages widget
	_messagesWidget->setMessages(_messages);
	
	// Scroll to bottom
	_scroll->scrollToY(_scroll->scrollTopMax());
}

void AIChatWidget::addAIMessage(const QString &text) {
	// Create a simple message entry
	_messages.push_back({
		.text = text,
		.isFromUser = false,
		.timestamp = base::unixtime::now()
	});

	// Update the messages widget
	_messagesWidget->setMessages(_messages);
	
	// Scroll to bottom
	_scroll->scrollToY(_scroll->scrollTopMax());
}

void AIChatWidget::paintEvent(QPaintEvent *e) {
	// The main widget doesn't need to paint messages anymore
	// as they are handled by the MessagesWidget inside the scroll area
	Painter p(this);
	// Use a custom light blue background for AI chat
	p.fillRect(rect(), kAIChatBackgroundColor);
}

void AIChatWidget::mousePressEvent(QMouseEvent *e) {
	// If clicking outside the messages widget, clear any selections
	if (_messagesWidget) {
		_messagesWidget->clearTextSelection();
		_messagesWidget->clearMessageSelection();
	}
}

void AIChatWidget::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	
	// Update messages widget width when the main widget is resized
	_messagesWidget->resizeToWidth(_scroll->width());
}

bool AIChatWidget::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	// This is a simple placeholder widget, so we don't handle any mementos
	return false;
}

QRect AIChatWidget::floatPlayerAvailableRect() {
	// Return the available rect for the float player
	return rect();
}

bool AIChatWidget::floatPlayerHandleWheelEvent(QEvent *e) {
	// Don't handle wheel events for the float player in this placeholder
	return false;
}

void AIChatWidget::updateControlsGeometry() {
	const auto composeHeight = _composeControls->heightCurrent();
	const auto clearButtonHeight = _clearButton->height();
	const auto scrollHeight = height() - composeHeight - clearButtonHeight - 5; // 5px spacing

	// Position clear button at the top right
	_clearButton->move(width() - 90, 0);
	_clearButton->resizeToWidth(80);

	// Position compose controls at the bottom
	_composeControls->move(0, height() - composeHeight);
	_composeControls->resizeToWidth(width());

	// Position scroll area below clear button and above compose controls
	_scroll->setGeometry(0, clearButtonHeight + 5, width(), scrollHeight);
}

void AIChatWidget::sendToOpenAI() {
	if (_openaiClient->isWaitingForResponse()) {
		return; // Don't send multiple requests simultaneously
	}

	// Load last 30 messages for context (main chat or topic), then send.
	_openaiClient->ensureContextLoaded([this] {
		// Add a "thinking" message to show the user that the AI is processing
		addAIMessage(kThinkingMessage);

		// Send the request using the OpenAI client
		_openaiClient->sendChatCompletion(
			_messages,
			[this](const QString &response) {
				// Replace the "thinking" message with the actual response
				if (!_messages.empty() && _messages.back().text == kThinkingMessage) {
					_messages.back().text = response;
					_messagesWidget->setMessages(_messages);
					// Scroll to bottom to show the full response
					_scroll->scrollToY(_scroll->scrollTopMax());
				} else {
					// Fallback: add the AI response to the chat
					addAIMessage(response);
				}
			},
			[this](const QString &error) {
				// Replace the "thinking" message with the error
				if (!_messages.empty() && _messages.back().text == kThinkingMessage) {
					QString errorMessage = QString("❌ Error: %1").arg(error);
					_messages.back().text = errorMessage;
					_messagesWidget->setMessages(_messages);
					// Scroll to bottom to show the full error message
					_scroll->scrollToY(_scroll->scrollTopMax());
				} else {
					// Fallback: add error message to chat
					addAIMessage(QString("❌ Error: %1").arg(error));
				}
			}
		);
	});
}

void AIChatWidget::clearChat() {
	// Clear all messages
	_messages.clear();
	_messagesWidget->setMessages(_messages);
	
	// Scroll to top since there are no messages
	_scroll->scrollToY(0);
	
	// Add welcome message back
	addAIMessage(kWelcomeMessage);
}
} // namespace AI
