#ifndef _MULTIPART_PARSER_H_
#define _MULTIPART_PARSER_H_

#include <string>
#include <stdexcept>
#include <cstring>

class MultipartParser {
public:
	typedef void (*Callback)(const char *buffer, size_t start, size_t end, void *userData);
	
private:
	static const char CR     = 13;
	static const char LF     = 10;
	static const char SPACE  = 32;
	static const char HYPHEN = 45;
	static const char COLON  = 58;
	static const size_t UNMARKED = (size_t) -1;
	
	enum State {
		ERROR,
		START,
		START_BOUNDARY,
		HEADER_FIELD_START,
		HEADER_FIELD,
		HEADER_VALUE_START,
		HEADER_VALUE,
		HEADER_VALUE_ALMOST_DONE,
		HEADERS_ALMOST_DONE,
		PART_DATA_START,
		PART_DATA,
		PART_END,
		END,
    ENDEEND
	};
	
	enum Flags {
		PART_BOUNDARY = 1,
		LAST_BOUNDARY = 2
	};
	
	std::string boundary;
	const char *boundaryData;
	size_t boundarySize;
	bool boundaryIndex[256];
	char *lookbehind;
	size_t lookbehindSize;
	State _state;
	int _flags;
	size_t _index;
	size_t headerFieldMark;
	size_t headerValueMark;
	size_t partDataMark;
	const char *errorReason;
	
	void resetCallbacks() {
		onPartBegin   = NULL;
		onHeaderField = NULL;
		onHeaderValue = NULL;
		onHeaderEnd   = NULL;
		onHeadersEnd  = NULL;
		onPartData    = NULL;
		onPartEnd     = NULL;
		onEnd         = NULL;
		userData      = NULL;
	}
	
	void indexBoundary() {
		const char *current;
		const char *end = boundaryData + boundarySize;
		
		memset(boundaryIndex, 0, sizeof(boundaryIndex));
		
		for (current = boundaryData; current < end; current++) {
			boundaryIndex[(unsigned char) *current] = true;
		}
	}
	
	void callback(Callback cb, const char *buffer = NULL, size_t start = UNMARKED,
		size_t end = UNMARKED, bool allowEmpty = false)
	{
		if (start != UNMARKED && start == end && !allowEmpty) {
			return;
		}
		if (cb != NULL) {
			cb(buffer, start, end, userData);
		}
	}
	
	void dataCallback(Callback cb, size_t &mark, const char *buffer, size_t i, size_t bufferLen,
		bool clear, bool allowEmpty = false)
	{
		if (mark == UNMARKED) {
			return;
		}
		
		if (!clear) {
			callback(cb, buffer, mark, bufferLen, allowEmpty);
			mark = 0;
		} else {
			callback(cb, buffer, mark, i, allowEmpty);
			mark = UNMARKED;
		}
	}
	
	char lower(char c) const {
		return c | 0x20;
	}
	
	inline bool isBoundaryChar(char c) const {
		return boundaryIndex[(unsigned char) c];
	}
	
	bool isHeaderFieldCharacter(char c) const {
		return (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| c == HYPHEN;
	}
	
	void setError(const char *message) {
		_state = ERROR;
		errorReason = message;
	}
	
	void processPartData(size_t &prevIndex, size_t &index, const char *buffer,
		size_t len, size_t boundaryEnd, size_t &i, char c, State &state, int &flags)
	{
		prevIndex = index;
		
		if (index == 0) {
			// boyer-moore derived algorithm to safely skip non-boundary data
			while (i + boundarySize <= len) {
				if (isBoundaryChar(buffer[i + boundaryEnd])) {
					break;
				}
				
				i += boundarySize;
			}
			if (i == len) {
				return;
			}
			c = buffer[i];
		}
		
		if (index < boundarySize) {
			if (boundary[index] == c) {
				if (index == 0) {
					dataCallback(onPartData, partDataMark, buffer, i, len, true);
				}
				index++;
			} else {
				index = 0;
			}
		} else if (index == boundarySize) {
			index++;
			if (c == CR) {
				// CR = part boundary
				flags |= PART_BOUNDARY;
			} else if (c == HYPHEN) {
				// HYPHEN = end boundary
				flags |= LAST_BOUNDARY;
			} else {
				index = 0;
			}
		} else if (index - 1 == boundarySize) {
			if (flags & PART_BOUNDARY) {
				index = 0;
				if (c == LF) {
					// unset the PART_BOUNDARY flag
					flags &= ~PART_BOUNDARY;
					callback(onPartEnd);
					callback(onPartBegin);
					state = HEADER_FIELD_START;
					return;
				}
			} else if (flags & LAST_BOUNDARY) {
				if (c == HYPHEN) {
                    callback(onPartEnd);
                    callback(onEnd);
                    state = END;
//printf("END %d %d\n", state, stopped());
				} else {
					index = 0;
				}
			} else {
				index = 0;
			}
		} else if (index - 2 == boundarySize) {
			if (c == CR) {
				index++;
			} else {
				index = 0;
			}
		} else if (index - boundarySize == 3) {
			index = 0;
			if (c == LF) {
				callback(onPartEnd);
				callback(onEnd);
				state = END;
				return;
			}
		}
		
		if (index > 0) {
			// when matching a possible boundary, keep a lookbehind reference
			// in case it turns out to be a false lead
			if (index - 1 >= lookbehindSize) {
				setError("Parser bug: index overflows lookbehind buffer. "
					"Please send bug report with input file attached.");
//				throw std::out_of_range("index overflows lookbehind buffer");
        return;
			} else if (index - 1 < 0) {
				setError("Parser bug: index underflows lookbehind buffer. "
					"Please send bug report with input file attached.");
//				throw std::out_of_range("index underflows lookbehind buffer");
        return;
			}
			lookbehind[index - 1] = c;
		} else if (prevIndex > 0) {
			// if our boundary turned out to be rubbish, the captured lookbehind
			// belongs to partData
			callback(onPartData, lookbehind, 0, prevIndex);
			prevIndex = 0;
			partDataMark = i;
			
			// reconsider the current character even so it interrupted the sequence
			// it could be the beginning of a new sequence
			i--;
		}
	}
	
public:
	Callback onPartBegin;
	Callback onHeaderField;
	Callback onHeaderValue;
	Callback onHeaderEnd;
	Callback onHeadersEnd;
	Callback onPartData;
	Callback onPartEnd;
	Callback onEnd;
	void *userData;
	
	MultipartParser() {
		lookbehind = NULL;
		resetCallbacks();
		reset();
	}
	
	MultipartParser(const std::string &boundary) {
		lookbehind = NULL;
		resetCallbacks();
		setBoundary(boundary);
	}
	
	~MultipartParser() {
		delete[] lookbehind;
	}
	
	void reset() {
		delete[] lookbehind;
		this->_state = ERROR;
		boundary.clear();
		boundaryData = boundary.c_str();
		boundarySize = 0;
		lookbehind = NULL;
		lookbehindSize = 0;
		this->_flags = 0;
		this->_index = 0;
		headerFieldMark = UNMARKED;
		headerValueMark = UNMARKED;
		partDataMark    = UNMARKED;
		errorReason     = "Parser uninitialized.";
	}
	
	void setBoundary(const std::string &boundary) {
		reset();
		this->boundary = "\r\n--" + boundary;
		boundaryData = this->boundary.c_str();
		boundarySize = this->boundary.size();
		indexBoundary();
		lookbehind = new char[boundarySize + 8];
		lookbehindSize = boundarySize + 8;
		this->_state = START;
		errorReason = "No error.";
	}
  
  int setBoundary(std::string *key, std::string *value) {
    int pos, ret = 1;
    std::string boundary;
    if(value->find("multipart/") != std::string::npos){
      pos = value->find("boundary=") + 9;
      boundary = value->substr(pos, std::string::npos);
      ret = 0;
    }
    if(!ret) 
      setBoundary(boundary);
    
    return ret;
  }
  
  int getFileInfos(const std::string &key, const std::string value, std::string &itemName, std::string &itemFilename, bool &itemIsFile) {
    int pos1,pos2,pos3, ret = 1;
    std::string _temp;
    _temp = value.substr(value.find(';') + 2);
    while(_temp.find(';') > 0) {
      pos1 = _temp.find('=');
      std::string name = _temp.substr(0, pos1);
      pos2 = _temp.find('"');
      pos3 = _temp.find('"', pos2+1);
      std::string nameVal = _temp.substr(pos2 + 1, pos3 - pos2 - 1);
      if(name == "name"){
        itemName = nameVal;
      } else if(name == "filename"){
        itemFilename = nameVal;
        itemIsFile = true;
        ret = 0;
        break;
      }
      _temp = _temp.substr(_temp.find(';') + 2);
    }
    return ret;
  }

	size_t feed(const char *buffer, size_t len) {
		if (this->_state == ERROR || len == 0) {
			return 0;
		}
		
		size_t prevIndex    = this->_index;
		size_t boundaryEnd  = boundarySize - 1;
		size_t i;
		char c, cl;
		
		for (i = 0; i < len; i++) {
			c = buffer[i];
			
			switch (this->_state) {
			case ERROR:
				return i;
			case START:
				this->_index = 0;
				this->_state = START_BOUNDARY;
			case START_BOUNDARY:
				if (this->_index == boundarySize - 2) {
					if (c != CR) {
						setError("Malformed. Expected CR after boundary.");
						return i;
					}
					this->_index++;
					break;
				} else if (this->_index - 1 == boundarySize - 2) {
					if (c != LF) {
						setError("Malformed. Expected LF after boundary CR.");
						return i;
					}
					this->_index = 0;
					callback(onPartBegin);
					this->_state = HEADER_FIELD_START;
					break;
				}
				if (c != boundary[this->_index + 2]) {
					setError("Malformed. Found different boundary data than the given one.");
					return i;
				}
				this->_index++;
				break;
			case HEADER_FIELD_START:
				this->_state = HEADER_FIELD;
				headerFieldMark = i;
				this->_index = 0;
			case HEADER_FIELD:
				if (c == CR) {
					headerFieldMark = UNMARKED;
					this->_state = HEADERS_ALMOST_DONE;
					break;
				}

				this->_index++;
				if (c == HYPHEN) {
					break;
				}

				if (c == COLON) {
					if (this->_index == 1) {
						// empty header field
						setError("Malformed first header name character.");
						return i;
					}
					dataCallback(onHeaderField, headerFieldMark, buffer, i, len, true);
					this->_state = HEADER_VALUE_START;
					break;
				}

				cl = lower(c);
				if (cl < 'a' || cl > 'z') {
					setError("Malformed header name.");
					return i;
				}
				break;
			case HEADER_VALUE_START:
				if (c == SPACE) {
					break;
				}
				
				headerValueMark = i;
				this->_state = HEADER_VALUE;
			case HEADER_VALUE:
				if (c == CR) {
					dataCallback(onHeaderValue, headerValueMark, buffer, i, len, true, true);
					callback(onHeaderEnd);
					this->_state = HEADER_VALUE_ALMOST_DONE;
				}
				break;
			case HEADER_VALUE_ALMOST_DONE:
				if (c != LF) {
					setError("Malformed header value: LF expected after CR");
					return i;
				}
				
				this->_state = HEADER_FIELD_START;
				break;
			case HEADERS_ALMOST_DONE:
				if (c != LF) {
					setError("Malformed header ending: LF expected after CR");
					return i;
				}
				
				callback(onHeadersEnd);
				this->_state = PART_DATA_START;
				break;
			case PART_DATA_START:
				this->_state = PART_DATA;
				partDataMark = i;
			case PART_DATA:
				processPartData(prevIndex, this->_index, buffer, len, boundaryEnd, i, c, this->_state, this->_flags);
				break;
			default:
				return i;
			}
		}
		
		dataCallback(onHeaderField, headerFieldMark, buffer, i, len, false);
		dataCallback(onHeaderValue, headerValueMark, buffer, i, len, false);
		dataCallback(onPartData, partDataMark, buffer, i, len, false);
		
		return len;
	}
	
	bool succeeded() const {
		return this->_state == END;
	}
	
	bool hasError() const {
		return this->_state == ERROR;
	}
	
	bool stopped() const {
		return this->_state == ERROR || this->_state == END;
	}
	
	const char *getErrorMessage() const {
		return errorReason;
	}
};

#endif /* _MULTIPART_PARSER_H_ */
