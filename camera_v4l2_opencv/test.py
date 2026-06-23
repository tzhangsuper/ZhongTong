import numpy as np
import matplotlib.pyplot as plt
from matplotlib import rcParams

# 设置中文字体
rcParams['font.sans-serif'] = ['SimHei']  # 使用黑体
rcParams['axes.unicode_minus'] = False  # 解决负号显示问题

class KMeans:
    def __init__(self, k=3, max_iters=100, tol=1e-8):
        """
        K-means聚类算法
        参数:
            k: 聚类数量
            max_iters: 最大迭代次数
            tol: 质心移动阈值（小于此值则停止迭代）
        """
        self.k = k
        self.max_iters = max_iters
        self.tol = tol
        self.centroids = None
        self.labels = None
        
    def fit(self, X):
        """
        训练K-means模型
        参数:
            X: 数据集，形状为(n_samples, n_features)
        """
        n_samples, n_features = X.shape
        
        # 步骤1: 初始化k个质心 - 采用给定数据集前k个点
        self.centroids = X[:self.k].copy()
        
        # 迭代更新
        for iteration in range(self.max_iters):
            # 步骤2: 分配样本到最近的质心
            self.labels = self._assign_clusters(X)
            
            # 检查是否存在空簇
            unique_labels = np.unique(self.labels)
            if len(unique_labels) < self.k:
                print(f"警告: 存在空簇，实际簇数为 {len(unique_labels)}")
            
            # 保存旧质心用于判断收敛
            old_centroids = self.centroids.copy()
            
            # 步骤3: 更新质心 - 计算每个簇内所有点的均值
            for i in range(self.k):
                cluster_points = X[self.labels == i]
                if len(cluster_points) > 0:
                    self.centroids[i] = cluster_points.mean(axis=0)
            
            # 步骤4: 检查收敛条件
            centroid_shift = np.linalg.norm(self.centroids - old_centroids)
            if centroid_shift < self.tol:
                print(f"算法收敛于第 {iteration + 1} 次迭代")
                break
                
        print(f"最终迭代次数: {iteration + 1}")
        
        return self
    
    def _assign_clusters(self, X):
        """
        根据距离函数分配样本到最近的质心
        距离定义: d(x,y) = sqrt(sum((x_k - y_k)^2) for k=1 to 4)
        """
        distances = np.zeros((X.shape[0], self.k))
        
        # 计算每个点到每个质心的距离
        for i in range(self.k):
            # 使用给定的距离公式
            distances[:, i] = np.sqrt(np.sum((X - self.centroids[i])**2, axis=1))
        
        # 返回距离最小的质心索引
        return np.argmin(distances, axis=1)
    
    def predict(self, X):
        """
        预测新数据的聚类标签
        """
        return self._assign_clusters(X)
    
    def get_cluster_counts(self):
        """
        获取每个簇的样本数量，并按从小到大排序
        """
        unique, counts = np.unique(self.labels, return_counts=True)
        counts_dict = dict(zip(unique, counts))
        
        # 确保所有簇都有计数（处理可能的空簇）
        for i in range(self.k):
            if i not in counts_dict:
                counts_dict[i] = 0
                
        # 按簇ID排序并返回计数
        sorted_counts = [counts_dict[i] for i in range(self.k)]
        return sorted_counts
    
def parse_input(input_str):
    """
    解析输入字符串，返回k值和数据矩阵
    输入格式：
        第一行：k n_samples max_iters tol
        后续行：每行4个特征值
    """
    lines = input_str.strip().split('\n')
    
    # 解析第一行参数
    params = list(map(float, lines[0].split()))
    k = int(params[0])
    n_samples = int(params[1])
    max_iters = int(params[2]) if len(params) > 2 else 100
    tol = 1e-8
    
    # 解析数据行
    data = []
    for line in lines[1:]:
        if line.strip():  # 跳过空行
            values = list(map(float, line.split()))
            data.append(values)
    
    X = np.array(data)
    
    return k, n_samples, max_iters, tol, X

if __name__ == "__main__":
    k, n_samples, max_iters, tol, X = parse_input(input_data)

    kmeans = KMeans(k=k, max_iters=max_iters, tol=tol)
    kmeans.fit(X)