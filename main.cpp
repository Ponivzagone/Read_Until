#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <utility>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <istream>
#include <iomanip>
#include <cstring>
#include <fstream>


int readn(int s, char * bp, int len)
{
    int cnt;
    int rc;

    cnt = len;

    while (cnt > 0)
    {
        rc = recv(s, bp, cnt, 0);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (rc == 0)
            return len - cnt;

        bp += rc;
        cnt -= rc;
    }
    return len;
}

std::pair<int, bool> partial_search(
    const char * review, int size1,
    const char * required, int size2)
{
    for (int i = 0; i < size1; ++i)
    {
        int try_i1 = i;
        int try_i2 = 0;
        for (;; ++try_i1, ++try_i2)
        {
            if (try_i2 == size2)
                return std::make_pair(i, true);
            if (try_i1 == size1)
            {
                if (try_i2 != 0)
                    return std::make_pair(i, false);
                else
                    break;
            }
            if (review[try_i1] != required[try_i2])
                break;
        }
    }
    return std::make_pair(size1, false);
}


/**
 * @return std::pair<int,int> first - position after end delim
 *                                        - size fill data
 */
std::pair<int,int> read_until(int s,
                  char * buffers,
                  int size1,
                  std::pair<int,int> fill_size,
                  const char * delim,
                  const int size2,
                  const int block_load)
{
    size1 -= fill_size.second;
    int end_size = fill_size.second;
    int search_position = fill_size.first;
    for (;;)
    {
        int end = end_size - search_position;

        std::pair<int, bool> result = partial_search(buffers + search_position, end, delim, size2);
        if (result.first != end)
        {
            if (result.second)
            {
                return std::pair<int,int>(result.first + search_position + size2, end_size);
            }
            else
            {
                search_position = result.first;
            }
        }
        else
        {
            search_position = end_size;
        }

        if (size1 == search_position)
        {
            std::cout << -1;
            return std::pair<int,int>(-1, end_size);
        }

        int r_l(0);
        if ((r_l = readn(s, buffers + end_size, block_load)) < 0)
        {
            return  std::pair<int,int>(r_l,end_size);
        }
        end_size += r_l;
    }
}


struct StreamBufferLight
{
    enum { MAXSIZE = 32768 };


    int size;
    int capacity;
    char * buff;
};

template<class InputIterator, class Size, class OutputIterator>
OutputIterator copy_n (InputIterator first, Size n, OutputIterator result)
{
  while (n>0) {
    *result = *first;
    ++result; ++first;
    --n;
  }
  return result;
}

#include <vector>


class mutable_buffer {
public:
  mutable_buffer() 
    : data_(0),
      size_(0)
  {
  }

  mutable_buffer(void* data, std::size_t size) 
    : data_(data),
      size_(size)
  {
  }

  void* data() const 
  {
    return data_;
  }

  std::size_t size() const 
  {
    return size_;
  }

  /**
   * @brief Перенос начало буфера на указанное число байт
   */
  mutable_buffer& operator+=(std::size_t n)
  {
    std::size_t offset = n < size_ ? n : size_;
    data_ = static_cast<char*>(data_) + offset;
    size_ -= offset;
    return *this;
  }

private:
  void * data_;
  std::size_t size_;

};


class basic_streambuf
  : public std::streambuf
{
public:

    explicit basic_streambuf(
        std::size_t maximum_size = (std::numeric_limits<std::size_t>::max)())
        : max_size_(maximum_size)
    {
        std::size_t pend = (std::min<std::size_t>)(max_size_, buffer_delta);
        buffer_.resize((std::max<std::size_t>)(pend, 1));
        setg(&buffer_[0], &buffer_[0], &buffer_[0]);
        setp(&buffer_[0], &buffer_[0] + pend);
    }

    /**
     * @brief Размер еще не прочитанных данных из потока
     */
    std::size_t size() const 
    {
        return pptr() - gptr();
    }

  
  /**
   * @brief Максимальный размер буфера
   */
  std::size_t max_size() const 
  {
    return max_size_;
  }

  /**
   * @brief текущее свободное место в буфере
   */
  std::size_t capacity() const BOOST_ASIO_NOEXCEPT
  {
    return buffer_.capacity();
  }


  /**
   * Ensures that the output sequence can accommodate @c n characters,
   * reallocating character array objects as necessary.
   *
   * @brief Выдает массив символов который гарантированно может содержать n - символов 
   *
   */
  mutable_buffer prepare(std::size_t n)
  {
    reserve(n);
    return mutable_buffer(pptr(), n * sizeof(char_type));
  }

  /// Move characters from the output sequence to the input sequence.
  /**
   * Appends @c n characters from the start of the output sequence to the input
   * sequence. The beginning of the output sequence is advanced by @c n
   * characters.
   *
   * Requires a preceding call <tt>prepare(x)</tt> where <tt>x >= n</tt>, and
   * no intervening operations that modify the input or output sequence.
   *
   * @note If @c n is greater than the size of the output sequence, the entire
   * output sequence is moved to the input sequence and no error is issued.
   */
  void commit(std::size_t n)
  {
    n = std::min<std::size_t>(n, epptr() - pptr());
    pbump(static_cast<int>(n));
    setg(eback(), gptr(), pptr());
  }

  /// Remove characters from the input sequence.
  /**
   * Removes @c n characters from the beginning of the input sequence.
   *
   * @note If @c n is greater than the size of the input sequence, the entire
   * input sequence is consumed and no error is issued.
   */
  void consume(std::size_t n)
  {
    if (egptr() < pptr())
      setg(&buffer_[0], gptr(), pptr());
    if (gptr() + n > pptr())
      n = pptr() - gptr();
    gbump(static_cast<int>(n));
  }

protected:
  enum { buffer_delta = 128 };

  /// Override std::streambuf behaviour.
  /**
   * Behaves according to the specification of @c std::streambuf::underflow().
   */
  int_type underflow()
  {
    if (gptr() < pptr())
    {
      setg(&buffer_[0], gptr(), pptr());
      return traits_type::to_int_type(*gptr());
    }
    else
    {
      return traits_type::eof();
    }
  }

  /// Override std::streambuf behaviour.
  /**
   * Behaves according to the specification of @c std::streambuf::overflow(),
   * with the specialisation that @c std::length_error is thrown if appending
   * the character to the input sequence would require the condition
   * <tt>size() > max_size()</tt> to be true.
   */
  int_type overflow(int_type c)
  {
    if (!traits_type::eq_int_type(c, traits_type::eof()))
    {
      if (pptr() == epptr())
      {
        std::size_t buffer_size = pptr() - gptr();
        if (buffer_size < max_size_ && max_size_ - buffer_size < buffer_delta)
        {
          reserve(max_size_ - buffer_size);
        }
        else
        {
          reserve(buffer_delta);
        }
      }

      *pptr() = traits_type::to_char_type(c);
      pbump(1);
      return c;
    }

    return traits_type::not_eof(c);
  }

  void reserve(std::size_t n)
  {
    // Get current stream positions as offsets.
    std::size_t gnext = gptr() - &buffer_[0];
    std::size_t pnext = pptr() - &buffer_[0];
    std::size_t pend = epptr() - &buffer_[0];

    // Check if there is already enough space in the put area.
    if (n <= pend - pnext)
    {
      return;
    }

    // Shift existing contents of get area to start of buffer.
    if (gnext > 0)
    {
      pnext -= gnext;
      std::memmove(&buffer_[0], &buffer_[0] + gnext, pnext);
    }

    // Ensure buffer is large enough to hold at least the specified size.
    if (n > pend - pnext)
    {
      if (n <= max_size_ && pnext <= max_size_ - n)
      {
        pend = pnext + n;
        buffer_.resize((std::max<std::size_t>)(pend, 1));
      }
      else
      {
        std::length_error ex("boost::asio::streambuf too long");
        boost::asio::detail::throw_exception(ex);
      }
    }

    // Update stream positions.
    setg(&buffer_[0], &buffer_[0], &buffer_[0] + pnext);
    setp(&buffer_[0] + pnext, &buffer_[0] + pend);
  }

private:
  std::size_t max_size_;
  std::vector<char_type> buffer_;
};






/**
 * @brief  По идее надо использовать потоковый буфер типа std::streambuf (закольцованного буфера)
 *         Проблемы переполнения по идее не должно возникнуть по RFC HTTP говорится что макс длинна пакета равно 2^15 (32768)
 */
int ReadChuncked(int socket, char * buffer, int size1, std::pair<int,int> fill_size, std::string & bodyResponse)
{
    int offset = fill_size.first;
    fill_size = read_until(socket, buffer, MAXSIZE, fill_size, "\r\n", 2, 2); // Прочитать данные до первого CRLF
    if(fill_size.first < 0) { return -1; }

    int byte(0);
    std::string tmp(buffer, offset, fill_size.first - offset - 2); // Подстрока с длинной пакета
    std::stringstream ss(tmp);  ss >> std::setbase(16) >> byte;
    int addByte(fill_size.second  - fill_size.first); // Данные уже прочитанные в буфер (начало данных пакета)

    while(addByte > 0 || byte > 0)
    {

        if(addByte <= 0) {
            int load_l(0);
            if( (load_l = readn(socket, buffer + fill_size.first, byte)) > 0) {
                fill_size.second += load_l;
                addByte += load_l;
            }
        }

        if( byte >= addByte )
        { 
          copy_n(buffer + fill_size.first, addByte, std::back_inserter(bodyResponse));
          byte -= addByte; addByte = 0; 
          fill_size.first  = 0;
          fill_size.second = 0;
        } else if( byte < addByte ) 
        { 
          copy_n(buffer + fill_size.first, byte, std::back_inserter(bodyResponse));
          addByte = addByte - byte;
          fill_size.first += byte;
          byte = 0;
        }       
        
        if(byte == 0) {
            fill_size = read_until(socket, buffer, MAXSIZE, fill_size, "\r\n", 2, 2);  // Skip Prev
            if(fill_size.first <= 0) { return -1; }
           
            
            offset = fill_size.first;
            
            fill_size = read_until(socket, buffer, MAXSIZE, fill_size,"\r\n", 2, 2);  // Read header for package
            if(fill_size.first <= 0) { return -1; }
            
            std::string tmp(buffer, offset, fill_size.first - offset - 2); // Get line with chunck len
            std::stringstream ss(tmp); ss >> std::setbase(16) >> byte;
            if(byte == 0) { return 0; }

            addByte = fill_size.second - fill_size.first;
            bodyResponse.reserve(bodyResponse.size() + byte);
        }
        
        sleep(2);
    }

    return 1;
}


int main(int argc, char **argv)
{
    int port;
    std::string ip;
    std::string url;
    if (argc != 3)
    {
        ip = "128.30.52.21";
        url = "/HTTP/ChunkedScript";
        port = 80;
        std::cout << "Usage: sync_client <128.30.52.21> <80> </HTTP/ChunkedScript>\n";
    }
    else
    {
        std::stringstream ss;
        ss << argv[2];
        ss >> port;
        ip = argv[1];
        url = argv[2];
    }
    std::stringstream ss;

    ss << "GET " << url << " HTTP/1.1\r\n";
    ss << "Host: " << ip << "\r\n";
    ss << "Accept: text/html\r\n";
    ss << "Connection: keep-alive\r\n";
    ss << "\r\n";

    char buff[MAXSIZE];

    int sock;
    struct sockaddr_in addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        exit(2);
    }

    if (send(sock, ss.str().c_str(), ss.str().length(), 0) == -1)
    {
        perror("send");
    }
    printf("Send errno: ");
    printf("%d\n", errno);

    
    std::pair<int, int> len = read_until(sock, buff, MAXSIZE, std::make_pair(0,0), "\r\n\r\n", 4,4096);
    
    char tmp = buff[len.first];
    buff[len.first] = '\0';
    std::cout << "receive: " << len.first << std::endl << "Data: " << buff << std::endl;
    buff[len.first] = tmp;
    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;
    

    //...
    // Parse Header;
    //...

    std::string bodyResponse;
    int rc = ReadChuncked(sock, buff, MAXSIZE, len, bodyResponse);

    std::cout << bodyResponse.size() << std::endl;

    std::fstream f;
    
    f.open("/root/Read_Until_Test/response");
    f  << bodyResponse << std::endl;
    f.close();

    close(sock);

    return 0;
}



/*
https://en.cppreference.com/w/cpp/io/basic_streambuf

https://www.boost.org/doc/libs/1_38_0/doc/html/boost_asio/reference/streambuf.html

https://github.com/boostorg/asio/tree/develop/include/boost/asio

https://github.com/boostorg/asio/blob/develop/include/boost/asio/buffer.hpp

https://github.com/boostorg/asio/blob/develop/include/boost/asio/basic_streambuf.hpp

https://github.com/boostorg/asio/blob/develop/include/boost/asio/impl/read_until.hpp

https://github.com/boostorg/asio/blob/develop/include/boost/asio/buffer.hpp

http://172.22.206.77/comm/ekasudclient/blob/master/main.cpp
*/