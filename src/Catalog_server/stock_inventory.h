#pragma once
#include<map>
#include<string>
#include<mutex>
#include<shared_mutex>
#define DEBUG(x) ((std::cout<<x).flush())
#define ABS(x) ((x)>0? (x):-(x))
struct StockInfo{ 
	std::string name;
	unsigned price;
	unsigned trade_num = 0;
	unsigned stock_remaining;
	const unsigned trade_num_limit=0;
	StockInfo(std::string name, unsigned price,unsigned trade_num, unsigned stock_remaining, unsigned trade_num_limit)
	:name(name), price(price), trade_num(trade_num), stock_remaining(stock_remaining), trade_num_limit(trade_num_limit) {}
	StockInfo(){}
};
class StockInventory {
	class Stock{
	private:
		StockInfo stock_info;
		mutable std::shared_mutex per_stock_lock;            //used to implement read-write lock

	public:	
		Stock(const StockInfo& stock_info)
			:stock_info(stock_info) {}
		Stock(const Stock& other) 
			:stock_info(other.stock_info){}//copy ctor wouldnt be auto generated because of mutex member
		StockInfo GetStockInfo() const{
			std::shared_lock lock(per_stock_lock); //acquire read lock
			return stock_info;
		}
		void SetPrice(unsigned price) {
			std::unique_lock lock(per_stock_lock); //acquire write lock
			stock_info.price= price;
		}
		int TradeStocks(int num){
			std::unique_lock lock(per_stock_lock); //acquire write lock
			if(num>(int)stock_info.stock_remaining){
				return -3;                                    //represents not enough stocks remaining
			}
			if(ABS(num)+stock_info.trade_num>stock_info.trade_num_limit){
				return -2;                                    //represents the operation is to cause trade num limit to be exceed
			}
			stock_info.stock_remaining-=num;
			stock_info.trade_num+=ABS(num);
			return 1;
		}
	};
	
private:
	typedef std::map<std::string,Stock> StockMap;
	StockMap m;
public:
	void AddStock(const StockInfo& stock_info) {
		m.emplace(stock_info.name, stock_info);
	}
	std::pair<int, StockInfo> LookUp(std::string name) {
		StockMap::iterator iter;
		if ((iter = m.find(name)) == m.end()) {
			return std::pair<int, StockInfo>(-1,StockInfo());
		}
		return std::pair<int,StockInfo>(1,iter->second.GetStockInfo());
	}
	int TradeStocks(std::string name, int num){
		StockMap::iterator iter;
		if ((iter = m.find(name)) == m.end()) {
			return -1;
		}
		return iter->second.TradeStocks(num);

	}
	int Update(std::string name, int new_price) {
		if (new_price < 0) return -2;
		StockMap::iterator iter;
		if ((iter = m.find(name)) == m.end()) {
			return -1;
		}
		iter->second.SetPrice(new_price);
		return 1;
	}
	class ConstIterator :public StockMap::const_iterator{
	public:
		ConstIterator(StockMap::const_iterator iter)
		:StockMap::const_iterator(iter){}
		const StockInfo operator*(){
			return StockMap::const_iterator::operator*().second.GetStockInfo();
		}
	};
	ConstIterator cbegin() const {
		return ConstIterator(m.cbegin());
	}
	ConstIterator cend() const{
		return ConstIterator(m.cend());
	}
};