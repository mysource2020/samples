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

#init data
insert into fms_user(id,name,password) values(1, 'test', '123456');

insert into fms_group(id,name,path_filter,create_time) values(1,'SYSTEM','',now());
insert into fms_config values(1,1,5, 'mon',  0, now());
insert into fms_config values(2,1,5, 'ssh',  0,   now());
insert into fms_config values(3,1,5, 'ips', '*', now());
insert into fms_config values(4,1,5, 'out', 'nic', now());
insert into fms_config values(5,1,5, 'in',  'nic:v2:t1:b1', now());
insert into fms_config values(6,1,5, 'psmin',  100,   now());
insert into fms_config values(7,1,5, 'psmax',  500,   now());
insert into fms_config values(8,1,5, 'hpmax',  50,   now());
insert into fms_config values(9,1,5, 'hdmax',  21,   now());
