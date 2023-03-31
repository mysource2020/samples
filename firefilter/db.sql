mysql -u root -p

create user fmsuser identified by 'qazxsw12';
grant all on *.* to 'fmsuser'@'%';

create database fmsdb;
use fmsdb;

create table fms_user
(
	id       INT PRIMARY KEY,
	name     VARCHAR(20),    
	password VARCHAR(20),     
	type     INT default 1,   
	status   INT default 1   
);

insert into fms_user(id,name,password) values(1, 'test', '123456');

create table fms_group
(
      id           INT PRIMARY KEY,
      name         VARCHAR(100) not null unique,  
      path_filter  VARCHAR(380),            
      te_filter    int default 0,           
      ip_repeat    int default 0,            
      start_time   VARCHAR(20)  default '00:00',  
      end_time     VARCHAR(20)  default '24:00',   
      description  VARCHAR(256) default '',       
      status       INT default 1,                 
      create_time  DATETIME                      
);

create table fms_config
(
	id         INT PRIMARY KEY,
	group_id   INT  not null,         
      type       INT not null,            
	source     VARCHAR(100) not null,   
	target     VARCHAR(100) default '', 
	create_time     DATETIME           
);

